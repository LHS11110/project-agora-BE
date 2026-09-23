#pragma once

#include <string>

// Identity established from the signed canvas token and the active DB user.
struct AuthenticatedUser {
    int user_id;
    int tag_number;
    std::string nickname;
    long long settings_revision{0};
};
