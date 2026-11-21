#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <locale>
#include <sstream>
#include <string>

namespace weather_editor_model {
// Sky/sun, clouds, fog, rain, wind, ambient. Every existing property remains editable.
inline constexpr unsigned weather_categories[] = {
    0,2,2,2,3,4,4,3,3,0,5,0,0,0,1,1,1,1,2,2,2,3,3,3,5,5,5,0,0,0,0,0,0,0,0,0,4,3,3
};
inline unsigned lightning_category(unsigned index) {
    return index<7?0:index<15?1:index<22?2:3;
}
inline bool parse_number(std::string text,float& value) {
    std::replace(text.begin(),text.end(),',','.');
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    float result;
    if(!(input>>result) || !std::isfinite(result)) return false;
    input>>std::ws;
    if(!input.eof()) return false;
    value=result;
    return true;
}
inline bool matches(std::string name,std::string query) {
    const auto fold=[](unsigned char c) {return c=='/'?'\\':static_cast<char>(std::tolower(c));};
    std::transform(name.begin(),name.end(),name.begin(),fold);
    std::transform(query.begin(),query.end(),query.begin(),fold);
    return name.find(query)!=std::string::npos;
}
} // namespace weather_editor_model
