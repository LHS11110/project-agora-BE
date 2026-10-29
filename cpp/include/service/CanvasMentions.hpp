#pragma once
#include <string>
#include <vector>
#include <set>
#include <cctype>
#include <limits>

inline bool mentionStartBoundary(char c) {
    return std::isspace(static_cast<unsigned char>(c)) || c=='(' || c=='[' || c=='{';
}
inline bool mentionEndBoundary(char c) {
    return std::isspace(static_cast<unsigned char>(c)) || std::string(".,!?;:)}]").find(c)!=std::string::npos;
}
inline std::vector<std::pair<std::string,int>> parseMentionHandles(const std::string& text) {
    std::vector<std::pair<std::string,int>> result;
    std::set<std::pair<std::string,int>> seen;
    for(std::size_t at=0;at<text.size() && result.size()<20;++at) {
        if(text[at]!='@' || (at && !mentionStartBoundary(text[at-1])))continue;
        for(std::size_t hash=at+1;hash<text.size() && hash-at<=401;++hash) {
            if(text[hash]=='\n' || text[hash]=='\r')break;
            if(text[hash]!='#' || hash==at+1)continue;
            std::size_t end=hash+1;long long tag=0;
            while(end<text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
                tag=tag*10+(text[end]-'0');if(tag>std::numeric_limits<int>::max())break;++end;
            }
            if(end==hash+1 || tag>std::numeric_limits<int>::max() || (end<text.size() && !mentionEndBoundary(text[end])))continue;
            auto value=std::make_pair(text.substr(at+1,hash-at-1),static_cast<int>(tag));
            if(seen.insert(value).second)result.push_back(value);
            break;
        }
    }
    return result;
}
