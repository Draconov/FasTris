#pragma once
#include <charconv>
#include <cstdint>
#include <string_view>
#include <system_error>

namespace fasttris {

// Scan the current clipboard/text payload from left to right. Each contiguous
// decimal run is treated as a candidate; overflowed candidates are skipped and
// scanning continues until a valid uint64 seed is found.
inline bool firstSeedInText(std::string_view text, std::uint64_t& out) {
    std::size_t i=0;
    while(i<text.size()){
        while(i<text.size()&&(text[i]<'0'||text[i]>'9'))++i;
        if(i==text.size())break;
        const std::size_t begin=i;
        while(i<text.size()&&text[i]>='0'&&text[i]<='9')++i;
        const auto candidate=text.substr(begin,i-begin);
        std::uint64_t value{};
        const auto* first=candidate.data();
        const auto* last=first+candidate.size();
        const auto result=std::from_chars(first,last,value,10);
        if(result.ec==std::errc{}&&result.ptr==last){out=value;return true;}
    }
    return false;
}

} // namespace fasttris
