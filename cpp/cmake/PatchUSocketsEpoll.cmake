set(USOCKETS_EPOLL_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/uWebSockets/uSockets/src/eventing/epoll_kqueue.c")
set(USOCKETS_EPOLL_PATCHED
    "${CMAKE_CURRENT_BINARY_DIR}/patched_usockets_epoll_kqueue.c")

file(READ "${USOCKETS_EPOLL_SOURCE}" USOCKETS_EPOLL_CONTENT)
set(USOCKETS_RESIZE_ORIGINAL [=[
    if (p != new_p && events) {
#ifdef LIBUS_USE_EPOLL
        /* Hack: forcefully update poll by stripping away already set events */
        new_p->state.poll_type = us_internal_poll_type(new_p);
        us_poll_change(new_p, loop, events);
#else
        /* Forcefully update poll by resetting them with new_p as user data */
        kqueue_change(loop->fd, new_p->state.fd, 0, events, new_p);
#endif

        /* This is needed for epoll also (us_change_poll doesn't update the old poll) */
        us_internal_loop_update_pending_ready_polls(loop, p, new_p, events, events);
    }
]=])
set(USOCKETS_RESIZE_PATCHED [=[
    if (p != new_p) {
#ifdef LIBUS_USE_EPOLL
        /* A paused socket has no subscribed events, but epoll still retains
         * data.ptr. Reallocating it without updating that pointer leaves an
         * epoll event referring to freed memory when the socket is upgraded. */
        struct epoll_event event;
        event.events = events;
        event.data.ptr = new_p;
        epoll_ctl(loop->fd, EPOLL_CTL_MOD, new_p->state.fd, &event);
#else
        /* No kqueue filters remain registered while the socket is paused. */
        if (events) {
            kqueue_change(loop->fd, new_p->state.fd, 0, events, new_p);
        }
#endif

        /* epoll_wait may already have returned an event with the old pointer. */
        us_internal_loop_update_pending_ready_polls(loop, p, new_p, events, events);
    }
]=])

string(FIND "${USOCKETS_EPOLL_CONTENT}" "${USOCKETS_RESIZE_ORIGINAL}" USOCKETS_RESIZE_POSITION)
if(USOCKETS_RESIZE_POSITION EQUAL -1)
    message(FATAL_ERROR "uSockets resize code changed; review PatchUSocketsEpoll.cmake")
endif()
string(REPLACE "${USOCKETS_RESIZE_ORIGINAL}" "${USOCKETS_RESIZE_PATCHED}"
    USOCKETS_EPOLL_CONTENT "${USOCKETS_EPOLL_CONTENT}")
file(WRITE "${USOCKETS_EPOLL_PATCHED}" "${USOCKETS_EPOLL_CONTENT}")
list(REMOVE_ITEM USOCKETS_SOURCES "${USOCKETS_EPOLL_SOURCE}")
list(APPEND USOCKETS_SOURCES "${USOCKETS_EPOLL_PATCHED}")
