#include "common/dsp/AnchorDSP.h"
#include "common/state/PluginSpec.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <iostream>

namespace {
class Harness final : public juce::AudioProcessor {
public:
 explicit Harness(const amanorsac::PluginSpec& s):AudioProcessor(BusesProperties().withInput("In",juce::AudioChannelSet::stereo(),true).withOutput("Out",juce::AudioChannelSet::stereo(),true)),state(*this,nullptr,"TEST",amanorsac::PluginSpec::createParameterLayout(s)){}
 void prepareToPlay(double,int)override{} void releaseResources()override{} bool isBusesLayoutSupported(const BusesLayout& l)const override{return l.getMainInputChannelSet()==l.getMainOutputChannelSet();}
 void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override{} juce::AudioProcessorEditor* createEditor()override{return nullptr;} bool hasEditor()const override{return false;}
 const juce::String getName()const override{return"PlateHarness";} bool acceptsMidi()const override{return false;} bool producesMidi()const override{return false;} bool isMidiEffect()const override{return false;}
 double getTailLengthSeconds()const override{return 0;} int getNumPrograms()override{return 1;} int getCurrentProgram()override{return 0;} void setCurrentProgram(int)override{}
 const juce::String getProgramName(int)override{return{};} void changeProgramName(int,const juce::String&)override{} void getStateInformation(juce::MemoryBlock&)override{} void setStateInformation(const void*,int)override{}
 juce::AudioProcessorValueTreeState state;
};
bool set(juce::AudioProcessorValueTreeState& s,const juce::String& id,float v){if(auto*p=s.getParameter(id)){p->setValueNotifyingHost(p->convertTo0to1(v));return true;}return false;}
int fail(const juce::String&m){std::cerr<<"FAIL: "<<m<<std::endl;return 1;}
}
int main(){
 const auto spec=amanorsac::PluginSpec::fromEmbeddedJson(); if(spec.id!="A10")return fail("Unexpected plugin"); Harness h(spec);
 set(h.state,"pre_delay",0); set(h.state,"decay",1.2f); set(h.state,"mix",100); set(h.state,"noise",0); set(h.state,"plate",3);
 for(const auto rate:{44100.0,96000.0,192000.0}) for(const auto blockSize:{32,256,1024}){
  amanorsac::AnchorDSP dsp; dsp.prepare(rate,blockSize,2); double tailEnergy=0; const auto blocks=static_cast<int>(std::ceil(rate*0.25/blockSize));
  for(int block=0;block<blocks;++block){juce::AudioBuffer<float>b(2,blockSize);b.clear();if(block==0){b.setSample(0,0,1);b.setSample(1,0,1);}dsp.process(b,h.state,"A10");
   for(int c=0;c<2;++c)for(int i=0;i<blockSize;++i){const auto v=b.getSample(c,i);if(!std::isfinite(v))return fail("Non-finite plate output");if(std::abs(v)>1.25f)return fail("Unbounded plate feedback");if(block>2)tailEnergy+=v*v;}}
  if(tailEnergy<1.0e-6)return fail("No plate tail at "+juce::String(rate)+" / "+juce::String(blockSize));
 }
 set(h.state,"mix",0); amanorsac::AnchorDSP dryDsp;dryDsp.prepare(48000,64,2);juce::AudioBuffer<float>b(2,64);b.clear();b.setSample(0,0,.5f);b.setSample(1,0,.5f);dryDsp.process(b,h.state,"A10");if(std::abs(b.getSample(0,0)-.5f)>1e-6f)return fail("Dry mix failed");
 set(h.state,"mix",100);set(h.state,"decay",8);set(h.state,"drive",100);set(h.state,"crosstalk",100);set(h.state,"width",200);
 amanorsac::AnchorDSP stress;stress.prepare(48000,256,2);float peak=0;
 for(int block=0;block<2250;++block){juce::AudioBuffer<float>x(2,256);x.clear();if(block==0){x.setSample(0,0,.9f);x.setSample(1,0,.9f);}stress.process(x,h.state,"A10");for(int c=0;c<2;++c)for(int i=0;i<256;++i){const auto v=x.getSample(c,i);if(!std::isfinite(v))return fail("Non-finite long plate tail");peak=juce::jmax(peak,std::abs(v));}}
 if(peak>2.05f)return fail("Unbounded long plate tail");
 set(h.state,"decay",1.2f);
 const auto saved=h.state.copyState();set(h.state,"decay",7);h.state.replaceState(saved.createCopy());if(std::abs(h.state.getRawParameterValue("decay")->load()-1.2f)>.01f)return fail("State failed");
 std::cout<<"PASS: A10 plate matrix"<<std::endl;return 0;
}
