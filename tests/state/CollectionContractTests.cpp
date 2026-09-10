#include "common/state/PluginSpec.h"
#include <iostream>
#include <set>
int main(){const auto s=amanorsac::PluginSpec::fromEmbeddedJson();int expected=0;juce::String terminal;
if(s.id=="D07"){expected=39;terminal="xover.05.frequency";}else if(s.id=="D08"){expected=48;terminal="tap.08.filter";}else if(s.id=="D10"){expected=18;terminal="xover.04.frequency";}else{std::cerr<<"FAIL unexpected";return 1;}
if(static_cast<int>(s.parameters.size())!=expected){std::cerr<<"FAIL count "<<s.parameters.size()<<" expected "<<expected;return 1;}std::set<std::string>ids;bool found=false;
for(const auto&p:s.parameters){if(!ids.insert(p.id.toStdString()).second){std::cerr<<"FAIL duplicate";return 1;}if(p.id==terminal)found=true;if(p.id=="zone.harmonics"||p.id=="tap.time"||p.id=="band.width"||p.id=="xover.frequency"){std::cerr<<"FAIL template ID";return 1;}}
if(!found){std::cerr<<"FAIL terminal";return 1;}juce::ignoreUnused(amanorsac::PluginSpec::createParameterLayout(s));std::cout<<"PASS: "<<s.id<<" indexed collection";return 0;}
