#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <cstdlib>
#include <csignal>
#include <cstdint>
#include <cerrno>
#include <charconv>
#include <optional>
#include <string_view>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <cstddef>
#include <execinfo.h>
#include <initializer_list>
#include <pthread.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include "service_memory/CanvasLifecycleMemory.hpp"
#include "service/CanvasControlService.hpp"
#include "ElasticsearchBulkLogBuffer.hpp"
#include "service/HealthService.hpp"
#include "api/HttpApiServer.hpp"
#include "service/CanvasQueryService.hpp"
#include "service/BrokerQuerySocketService.hpp"
#include "service_memory/RegistryServiceMemory.hpp"
#include "Environment.hpp"

static HttpApiServer* g_server = nullptr;
static BrokerQuerySocketService* g_broker_server = nullptr;
static std::atomic<bool> g_cleanup_running{true};
static std::mutex g_cleanup_mutex;
static std::condition_variable g_cleanup_cv;
static std::atomic<bool> g_graceful_shutdown{false};
static std::atomic<bool> g_shutdown_started{false};

static std::string g_advertise_ip = "127.0.0.1";
static int g_port = 8000;
static std::string g_db_host = "127.0.0.1";
static int g_db_port = 1433;

namespace {
std::optional<int> parseInteger(std::string_view value, int minimum, int maximum) {
    int parsed = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size()
            || parsed < minimum || parsed > maximum) {
        return std::nullopt;
    }
    return parsed;
}

bool readIntegerEnvironment(const char* name, int& target, int minimum, int maximum) {
    const char* configured = std::getenv(name);
    if (!configured) return true;
    const auto parsed = parseInteger(configured, minimum, maximum);
    if (!parsed) {
        std::cerr << "Invalid integer setting: " << name << "\n";
        return false;
    }
    target = *parsed;
    return true;
}

bool readIntegerArgument(const char* value, const char* name, int& target, int minimum, int maximum) {
    const auto parsed = parseInteger(value, minimum, maximum);
    if (!parsed) {
        std::cerr << "Invalid integer argument: " << name << "\n";
        return false;
    }
    target = *parsed;
    return true;
}

void writeSignalText(const char* text, std::size_t length) {
    while (length > 0) {
        const ssize_t written = write(STDERR_FILENO, text, length);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return;
        text += written;
        length -= static_cast<std::size_t>(written);
    }
}

void writeSignalNumber(const char* label, long long value) {
    char buffer[32];
    std::size_t position = sizeof(buffer);
    const bool negative = value < 0;
    unsigned long long magnitude = negative
        ? static_cast<unsigned long long>(-(value + 1)) + 1
        : static_cast<unsigned long long>(value);
    do {
        buffer[--position] = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude);
    if (negative) buffer[--position] = '-';
    writeSignalText(label, std::char_traits<char>::length(label));
    writeSignalText(buffer + position, sizeof(buffer) - position);
    writeSignalText("\n", 1);
}

void writeSignalHex(const char* label, std::uintptr_t value) {
    static constexpr char digits[] = "0123456789abcdef";
    char buffer[2 + sizeof(value) * 2];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (std::size_t i = 0; i < sizeof(value) * 2; ++i) {
        const std::size_t shift = (sizeof(value) * 2 - i - 1) * 4;
        buffer[2 + i] = digits[(value >> shift) & 0xf];
    }
    writeSignalText(label, std::char_traits<char>::length(label));
    writeSignalText(buffer, sizeof(buffer));
    static constexpr char newline[] = "\n";
    writeSignalText(newline, sizeof(newline) - 1);
}

void crashTraceHandler(int signal_number, siginfo_t* signal_info, void* raw_context) {
    const char* signal_name = "fatal signal";
    std::size_t name_length = sizeof("fatal signal") - 1;
    switch (signal_number) {
        case SIGABRT:
            signal_name = "SIGABRT (allocator abort)";
            name_length = sizeof("SIGABRT (allocator abort)") - 1;
            break;
        case SIGSEGV:
            signal_name = "SIGSEGV (segmentation fault)";
            name_length = sizeof("SIGSEGV (segmentation fault)") - 1;
            break;
        case SIGBUS: signal_name = "SIGBUS"; name_length = sizeof("SIGBUS") - 1; break;
        case SIGILL: signal_name = "SIGILL"; name_length = sizeof("SIGILL") - 1; break;
        case SIGFPE: signal_name = "SIGFPE"; name_length = sizeof("SIGFPE") - 1; break;
    }
    static constexpr char prefix[] = "\n[CrashTrace] ";
    writeSignalText(prefix, sizeof(prefix) - 1);
    writeSignalText(signal_name, name_length);
    static constexpr char suffix[] = "\n";
    writeSignalText(suffix, sizeof(suffix) - 1);

    // Emit context before unwinding: backtrace itself may fail if the heap or
    // stack is corrupt. These calls do not allocate or take C++ locks.
    timespec now{};
    if (clock_gettime(CLOCK_REALTIME, &now) == 0) {
        writeSignalNumber("[CrashTrace] unix_seconds=", now.tv_sec);
        writeSignalNumber("[CrashTrace] nanoseconds=", now.tv_nsec);
    }
    writeSignalNumber("[CrashTrace] pid=", getpid());
    writeSignalNumber("[CrashTrace] tid=", syscall(SYS_gettid));
    char thread_name[16]{};
    if (syscall(SYS_prctl, PR_GET_NAME, thread_name, 0, 0, 0) == 0) {
        static constexpr char thread_prefix[] = "[CrashTrace] thread_name=";
        writeSignalText(thread_prefix, sizeof(thread_prefix) - 1);
        std::size_t length = 0;
        while (length < sizeof(thread_name) && thread_name[length]) ++length;
        writeSignalText(thread_name, length);
        writeSignalText("\n", 1);
    }
    writeSignalNumber("[CrashTrace] signal_number=", signal_number);

    if (signal_info) {
        writeSignalNumber("[CrashTrace] signal_code=", signal_info->si_code);
        writeSignalNumber("[CrashTrace] signal_errno=", signal_info->si_errno);
        if (signal_number == SIGSEGV || signal_number == SIGBUS
            || signal_number == SIGILL || signal_number == SIGFPE) {
            writeSignalHex("[CrashTrace] fault_address=", reinterpret_cast<std::uintptr_t>(signal_info->si_addr));
        } else if (signal_number == SIGABRT) {
            writeSignalNumber("[CrashTrace] sender_pid=", signal_info->si_pid);
            writeSignalNumber("[CrashTrace] sender_uid=", signal_info->si_uid);
        }
    }
#if defined(__x86_64__) && defined(REG_RIP)
    if (raw_context) {
        const auto* context = static_cast<const ucontext_t*>(raw_context);
#define TRACE_REGISTER(name, index) \
        writeSignalHex("[CrashTrace] " name "=", static_cast<std::uintptr_t>(context->uc_mcontext.gregs[index]))
        TRACE_REGISTER("rip", REG_RIP);
        TRACE_REGISTER("rsp", REG_RSP);
        TRACE_REGISTER("rbp", REG_RBP);
        TRACE_REGISTER("rax", REG_RAX);
        TRACE_REGISTER("rbx", REG_RBX);
        TRACE_REGISTER("rcx", REG_RCX);
        TRACE_REGISTER("rdx", REG_RDX);
        TRACE_REGISTER("rdi", REG_RDI);
        TRACE_REGISTER("rsi", REG_RSI);
        TRACE_REGISTER("r8", REG_R8);
        TRACE_REGISTER("r9", REG_R9);
        TRACE_REGISTER("r10", REG_R10);
        TRACE_REGISTER("r11", REG_R11);
        TRACE_REGISTER("r12", REG_R12);
        TRACE_REGISTER("r13", REG_R13);
        TRACE_REGISTER("r14", REG_R14);
        TRACE_REGISTER("r15", REG_R15);
        TRACE_REGISTER("eflags", REG_EFL);
        TRACE_REGISTER("error_code", REG_ERR);
        TRACE_REGISTER("trap_number", REG_TRAPNO);
#undef TRACE_REGISTER
    }
#endif

    static constexpr char stack_header[] = "[CrashTrace] stack_trace:\n";
    writeSignalText(stack_header, sizeof(stack_header) - 1);
    void* frames[64];
    const int frame_count = backtrace(frames, static_cast<int>(sizeof(frames) / sizeof(frames[0])));
    if (frame_count > 0) backtrace_symbols_fd(frames, frame_count, STDERR_FILENO);

    // Restore the default fatal-signal behavior so the process still produces
    // a core dump when the host permits it.
    struct sigaction default_action{};
    default_action.sa_handler = SIG_DFL;
    sigemptyset(&default_action.sa_mask);
    (void)sigaction(signal_number, &default_action, nullptr);
    sigset_t unblocked;
    sigemptyset(&unblocked);
    sigaddset(&unblocked, signal_number);
    (void)sigprocmask(SIG_UNBLOCK, &unblocked, nullptr);
    (void)raise(signal_number);
    _exit(128 + signal_number);
}

void installCrashTraceHandlers() {
    struct sigaction action{};
    action.sa_sigaction = crashTraceHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    for (const int signal_number : {SIGABRT, SIGSEGV, SIGBUS, SIGILL, SIGFPE}) {
        (void)sigaction(signal_number, &action, nullptr);
    }
}
}

void stop_servers(CanvasLifecycleMemory& canvas_pool) {
    if (g_shutdown_started.exchange(true)) return;

    {
        std::lock_guard<std::mutex> lock(g_cleanup_mutex);
        g_cleanup_running = false;
    }
    g_cleanup_cv.notify_all();

    RegistryServiceMemory(g_db_host, g_db_port).setServerInactive(g_advertise_ip, g_port);
    std::cout << "[Agora C++ Server] 서버를 DB에서 비활성화했습니다.\n";

    if (g_server) g_server->stop();
    if (g_broker_server) g_broker_server->stop();

    if (!canvas_pool.saveCanvasesForShutdown()) {
        std::cerr << "[Agora C++ Server] 일부 Canvas를 Elasticsearch에 저장하지 못했습니다. "
                     "Redis 캐시는 보존한 채 종료를 계속합니다.\n";
    }


    // Close callbacks have now cleared SQL sessions. Uncache each saved canvas
    // so another server can load it before clients reconnect.
    canvas_pool.cleanupInactiveCanvases();
}

int main(int argc, char* argv[]) {
    installCrashTraceHandlers();
    struct sigaction ignore_sigpipe{};
    ignore_sigpipe.sa_handler = SIG_IGN;
    sigemptyset(&ignore_sigpipe.sa_mask);
    if (sigaction(SIGPIPE, &ignore_sigpipe, nullptr) != 0) {
        std::cerr << "Could not disable SIGPIPE delivery for network clients\n";
        return 1;
    }
    pthread_setname_np(pthread_self(), "agora-main");

    sigset_t handled_signals;
    sigemptyset(&handled_signals);
    sigaddset(&handled_signals, SIGINT);
    sigaddset(&handled_signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &handled_signals, nullptr);

    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string host = "0.0.0.0";
    std::string es_host = "127.0.0.1";
    int es_port = 9200;
    std::string java_host = "127.0.0.1";
    int java_port = 8080;

    if (const char* env_host = std::getenv("HOST")) host = env_host;
    if (const char* env_adv_ip = std::getenv("ADVERTISE_IP")) g_advertise_ip = env_adv_ip;
    if (!readIntegerEnvironment("PORT", g_port, 1, 65535)) return 1;
    if (const char* env_db_host = std::getenv("DB_HOST")) g_db_host = env_db_host;
    if (!readIntegerEnvironment("DB_PORT", g_db_port, 1, 65535)) return 1;
    if (const char* env_es_host = std::getenv("ES_HOST")) es_host = env_es_host;
    if (!readIntegerEnvironment("ES_PORT", es_port, 1, 65535)) return 1;
    if (const char* env_java_host = std::getenv("JAVA_HOST")) java_host = env_java_host;
    if (!readIntegerEnvironment("JAVA_PORT", java_port, 1, 65535)) return 1;



    const std::string internal_api_token = environmentValue("CPP_INTERNAL_API_TOKEN");
    if (internal_api_token.size() < 32) {
        std::cerr << "CPP_INTERNAL_API_TOKEN must be configured and at least 32 characters long\n";
        return 1;
    }

    int ws_port = g_port + 2;

    if (argc >= 5) {
        host = argv[1];
        g_advertise_ip = argv[2];
        if (!readIntegerArgument(argv[3], "REST port", g_port, 1, 65535)
                || !readIntegerArgument(argv[4], "WebSocket port", ws_port, 1, 65535)) return 1;
    } else {
        if (argc > 1) host = argv[1];
        if (argc > 2) g_advertise_ip = argv[2];
        if (argc > 3 && !readIntegerArgument(argv[3], "REST port", g_port, 1, 65535)) return 1;
    }
    if (!readIntegerEnvironment("WS_PORT", ws_port, 1, 65535)) return 1;
    if (ws_port < 1 || ws_port > 65535) {
        std::cerr << "Derived WebSocket port is outside the valid range; configure WS_PORT explicitly.\n";
        return 1;
    }

    if (g_advertise_ip == "127.0.0.1" && host != "0.0.0.0") {
        g_advertise_ip = host;
    }
    if (g_advertise_ip == "127.0.0.1" && std::getenv("ADVERTISE_IP") == nullptr && argc <= 2) {
        g_advertise_ip = (host == "0.0.0.0" ? "127.0.0.1" : host);
    }

    std::cout << "========================================\n";
    std::cout << " Agora C++ Canvas Query Server\n";
    std::cout << " Process PID:            " << getpid() << "\n";
    std::cout << " REST Listening on:      " << host << ":" << g_port << "\n";
    std::cout << " Broker routing key:     " << host << ":" << ws_port << "\n";
    std::cout << " MSSQL:                  " << g_db_host << ":" << g_db_port << "\n";
    std::cout << " ES:                     " << es_host << ":" << es_port << "\n";
    std::cout << " Java API:               " << java_host << ":" << java_port << "\n";
    std::cout << "----------------------------------------\n";
    std::cout << " [OSS Licenses & Attributions]\n";
    std::cout << " - uWebSockets & uSockets (Apache-2.0, (c) Alex Hultman)\n";
    std::cout << " - cpp-httplib & nlohmann/json (MIT)\n";
    std::cout << " - FreeTDS sybdb (LGPL-2.1+, see https://www.freetds.org/)\n";
    std::cout << " - zlib (zlib license)\n";
    std::cout << " See THIRD_PARTY_LICENSES.md for full license texts.\n";
    std::cout << "========================================\n";

    // Start the batched ES log sink before startup and storage logs are emitted.
    auto& es_log_sink = ElasticsearchBulkLogBuffer::instance();
    ElasticsearchLogStreamCapture es_log_capture(es_log_sink);

    CanvasLifecycleMemory canvas_pool(g_db_host, g_db_port, es_host, es_port, java_host, java_port, g_advertise_ip, g_port);

    RegistryServiceMemory mssql(g_db_host, g_db_port);
    if (!mssql.registerServer(g_advertise_ip, g_port, ws_port)) {
        std::cerr << "[RegistryServiceMemory] Failed to register server to DB.\n";
        return 1;
    } else {
        std::cout << "[RegistryServiceMemory] Successfully registered server to DB (IP: " << g_advertise_ip << ", REST: " << g_port << ", WS: " << ws_port << ")\n";
    }

    std::vector<std::unique_ptr<HttpServiceModule>> api_modules;
    api_modules.push_back(std::make_unique<CanvasControlService>(canvas_pool));
    CanvasQueryService queries(canvas_pool, ws_port);
    api_modules.push_back(std::make_unique<HealthService>());
    HttpApiServer server(host, g_port, std::move(api_modules));
    BrokerQuerySocketService broker_socket(queries, host, ws_port, environmentValue("CPP_INTERNAL_API_TOKEN"));
    if (!broker_socket.start()) return 1;
    g_server = &server;
    g_broker_server = &broker_socket;

    std::thread http_server_thread([&server]() {
        server.start();
    });

    std::thread signal_thread([&]() {
        pthread_setname_np(pthread_self(), "agora-signal");
        while (g_cleanup_running) {
            timespec timeout{1, 0};
            int signal = sigtimedwait(&handled_signals, nullptr, &timeout);
            if (signal != SIGINT && signal != SIGTERM) continue;

            std::cout << "\n[Agora C++ Server] Caught signal " << signal << ", shutting down..." << std::endl;
            g_graceful_shutdown = true;
            stop_servers(canvas_pool);
            break;
        }
    });

    std::thread cleanup_thread([&]() {
        pthread_setname_np(pthread_self(), "agora-cleanup");
        while (g_cleanup_running) {
            if (!g_graceful_shutdown && !mssql.heartbeatServer(g_advertise_ip, g_port)) {
                std::cerr << "[Agora C++ Server] Server heartbeat update failed\n";
            }
            if (!g_graceful_shutdown) canvas_pool.cleanupInactiveCanvases();

            std::unique_lock<std::mutex> lock(g_cleanup_mutex);
            g_cleanup_cv.wait_for(lock, std::chrono::seconds(5), [] { return !g_cleanup_running || g_graceful_shutdown; });
        }
    });

    if (http_server_thread.joinable()) http_server_thread.join();

    {
        std::lock_guard<std::mutex> lock(g_cleanup_mutex);
        g_cleanup_running = false;
    }
    g_cleanup_cv.notify_all();
    if (cleanup_thread.joinable()) {
        cleanup_thread.join();
    }
    if (signal_thread.joinable()) {
        signal_thread.join();
    }

    std::cout << "[Agora C++ Server] Server stopped gracefully." << std::endl;
    return 0;
}
