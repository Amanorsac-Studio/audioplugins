#include "AnchorDSP.h"

namespace amanorsac
{
namespace
{
float gainFromDb(float decibels) noexcept
{
    return juce::Decibels::decibelsToGain(decibels, -100.0f);
}
}

void AnchorDSP::Biquad::reset() noexcept
{
    z1 = 0.0f;
    z2 = 0.0f;
}

void AnchorDSP::Biquad::setPeak(double rate, float frequency, float q, float gainLinear) noexcept
{
    const auto omega = juce::MathConstants<double>::twoPi * static_cast<double>(frequency) / rate;
    const auto sine = static_cast<float>(std::sin(omega));
    const auto cosine = static_cast<float>(std::cos(omega));
    const auto alpha = sine / (2.0f * juce::jmax(0.001f, q));
    const auto amplitude = std::sqrt(juce::jmax(0.000001f, gainLinear));
    const auto a0 = 1.0f + alpha / amplitude;
    b0 = (1.0f + alpha * amplitude) / a0;
    b1 = (-2.0f * cosine) / a0;
    b2 = (1.0f - alpha * amplitude) / a0;
    a1 = (-2.0f * cosine) / a0;
    a2 = (1.0f - alpha / amplitude) / a0;
}

void AnchorDSP::Biquad::setBandPass(double rate, float frequency, float q) noexcept
{
    const auto omega = juce::MathConstants<double>::twoPi * static_cast<double>(frequency) / rate;
    const auto sine = static_cast<float>(std::sin(omega));
    const auto cosine = static_cast<float>(std::cos(omega));
    const auto alpha = sine / (2.0f * juce::jmax(0.001f, q));
    const auto a0 = 1.0f + alpha;
    b0 = alpha / a0;
    b1 = 0.0f;
    b2 = -b0;
    a1 = (-2.0f * cosine) / a0;
    a2 = (1.0f - alpha) / a0;
}

void AnchorDSP::Biquad::setNotch(double rate, float frequency, float q) noexcept
{
    const auto omega = juce::MathConstants<double>::twoPi * static_cast<double>(frequency) / rate;
    const auto sine = static_cast<float>(std::sin(omega));
    const auto cosine = static_cast<float>(std::cos(omega));
    const auto alpha = sine / (2.0f * juce::jmax(0.001f, q));
    const auto normaliser = 1.0f / (1.0f + alpha);
    b0 = normaliser;
    b1 = -2.0f * cosine * normaliser;
    b2 = normaliser;
    a1 = b1;
    a2 = (1.0f - alpha) * normaliser;
}

void AnchorDSP::Biquad::setLowPass(double rate, float frequency, float q) noexcept
{
    const auto omega = juce::MathConstants<double>::twoPi * static_cast<double>(frequency) / rate;
    const auto sine = static_cast<float>(std::sin(omega));
    const auto cosine = static_cast<float>(std::cos(omega));
    const auto alpha = sine / (2.0f * q);
    const auto a0 = 1.0f + alpha;
    b0 = ((1.0f - cosine) * 0.5f) / a0;
    b1 = (1.0f - cosine) / a0;
    b2 = b0;
    a1 = (-2.0f * cosine) / a0;
    a2 = (1.0f - alpha) / a0;
}

void AnchorDSP::Biquad::setHighPass(double rate, float frequency, float q) noexcept
{
    const auto omega = juce::MathConstants<double>::twoPi * static_cast<double>(frequency) / rate;
    const auto sine = static_cast<float>(std::sin(omega));
    const auto cosine = static_cast<float>(std::cos(omega));
    const auto alpha = sine / (2.0f * q);
    const auto a0 = 1.0f + alpha;
    b0 = ((1.0f + cosine) * 0.5f) / a0;
    b1 = (-(1.0f + cosine)) / a0;
    b2 = b0;
    a1 = (-2.0f * cosine) / a0;
    a2 = (1.0f - alpha) / a0;
}

void AnchorDSP::Biquad::setLowShelf(double rate, float frequency, float gainLinear) noexcept
{
    const auto amplitude = std::sqrt(juce::jmax(0.000001f, gainLinear));
    const auto omega = juce::MathConstants<double>::twoPi * static_cast<double>(frequency) / rate;
    const auto cosine = static_cast<float>(std::cos(omega));
    const auto sine = static_cast<float>(std::sin(omega));
    const auto alphaTerm = sine * std::sqrt(2.0f * amplitude);
    const auto a0 = (amplitude + 1.0f) + (amplitude - 1.0f) * cosine + alphaTerm;
    b0 = amplitude * ((amplitude + 1.0f) - (amplitude - 1.0f) * cosine + alphaTerm) / a0;
    b1 = 2.0f * amplitude * ((amplitude - 1.0f) - (amplitude + 1.0f) * cosine) / a0;
    b2 = amplitude * ((amplitude + 1.0f) - (amplitude - 1.0f) * cosine - alphaTerm) / a0;
    a1 = -2.0f * ((amplitude - 1.0f) + (amplitude + 1.0f) * cosine) / a0;
    a2 = ((amplitude + 1.0f) + (amplitude - 1.0f) * cosine - alphaTerm) / a0;
}

void AnchorDSP::Biquad::setHighShelf(double rate, float frequency, float gainLinear) noexcept
{
    const auto amplitude = std::sqrt(juce::jmax(0.000001f, gainLinear));
    const auto omega = juce::MathConstants<double>::twoPi * static_cast<double>(frequency) / rate;
    const auto cosine = static_cast<float>(std::cos(omega));
    const auto sine = static_cast<float>(std::sin(omega));
    const auto alphaTerm = sine * std::sqrt(2.0f * amplitude);
    const auto a0 = (amplitude + 1.0f) - (amplitude - 1.0f) * cosine + alphaTerm;
    b0 = amplitude * ((amplitude + 1.0f) + (amplitude - 1.0f) * cosine + alphaTerm) / a0;
    b1 = -2.0f * amplitude * ((amplitude - 1.0f) + (amplitude + 1.0f) * cosine) / a0;
    b2 = amplitude * ((amplitude + 1.0f) + (amplitude - 1.0f) * cosine - alphaTerm) / a0;
    a1 = 2.0f * ((amplitude - 1.0f) - (amplitude + 1.0f) * cosine) / a0;
    a2 = ((amplitude + 1.0f) - (amplitude - 1.0f) * cosine - alphaTerm) / a0;
}

void AnchorDSP::Biquad::setLowShelf(double rate, float frequency, float gainLinear, float slope) noexcept
{
    const auto amplitude = std::sqrt(juce::jmax(0.000001f, gainLinear));
    const auto omega = juce::MathConstants<double>::twoPi * static_cast<double>(frequency) / rate;
    const auto cosine = static_cast<float>(std::cos(omega));
    const auto sine = static_cast<float>(std::sin(omega));
    const auto s = juce::jlimit(0.05f, 4.0f, slope);
    const auto alpha = sine * 0.5f * std::sqrt(juce::jmax(0.0001f, (amplitude + 1.0f / amplitude) * (1.0f / s - 1.0f) + 2.0f));
    const auto beta = 2.0f * std::sqrt(amplitude) * alpha;
    const auto a0 = (amplitude + 1.0f) + (amplitude - 1.0f) * cosine + beta;
    b0 = amplitude * ((amplitude + 1.0f) - (amplitude - 1.0f) * cosine + beta) / a0;
    b1 = 2.0f * amplitude * ((amplitude - 1.0f) - (amplitude + 1.0f) * cosine) / a0;
    b2 = amplitude * ((amplitude + 1.0f) - (amplitude - 1.0f) * cosine - beta) / a0;
    a1 = -2.0f * ((amplitude - 1.0f) + (amplitude + 1.0f) * cosine) / a0;
    a2 = ((amplitude + 1.0f) + (amplitude - 1.0f) * cosine - beta) / a0;
}

void AnchorDSP::Biquad::setHighShelf(double rate, float frequency, float gainLinear, float slope) noexcept
{
    const auto amplitude = std::sqrt(juce::jmax(0.000001f, gainLinear));
    const auto omega = juce::MathConstants<double>::twoPi * static_cast<double>(frequency) / rate;
    const auto cosine = static_cast<float>(std::cos(omega));
    const auto sine = static_cast<float>(std::sin(omega));
    const auto s = juce::jlimit(0.05f, 4.0f, slope);
    const auto alpha = sine * 0.5f * std::sqrt(juce::jmax(0.0001f, (amplitude + 1.0f / amplitude) * (1.0f / s - 1.0f) + 2.0f));
    const auto beta = 2.0f * std::sqrt(amplitude) * alpha;
    const auto a0 = (amplitude + 1.0f) - (amplitude - 1.0f) * cosine + beta;
    b0 = amplitude * ((amplitude + 1.0f) + (amplitude - 1.0f) * cosine + beta) / a0;
    b1 = -2.0f * amplitude * ((amplitude - 1.0f) + (amplitude + 1.0f) * cosine) / a0;
    b2 = amplitude * ((amplitude + 1.0f) + (amplitude - 1.0f) * cosine - beta) / a0;
    a1 = 2.0f * ((amplitude - 1.0f) - (amplitude + 1.0f) * cosine) / a0;
    a2 = ((amplitude + 1.0f) - (amplitude - 1.0f) * cosine - beta) / a0;
}

void AnchorDSP::Biquad::setFirstOrderLowPass(double rate, float frequency) noexcept
{
    const auto k = static_cast<float>(std::tan(juce::MathConstants<double>::pi * static_cast<double>(frequency) / rate));
    const auto norm = 1.0f / (1.0f + k);
    b0 = k * norm; b1 = b0; b2 = 0.0f;
    a1 = (k - 1.0f) * norm; a2 = 0.0f;
}

void AnchorDSP::Biquad::setFirstOrderHighPass(double rate, float frequency) noexcept
{
    const auto k = static_cast<float>(std::tan(juce::MathConstants<double>::pi * static_cast<double>(frequency) / rate));
    const auto norm = 1.0f / (1.0f + k);
    b0 = norm; b1 = -norm; b2 = 0.0f;
    a1 = (k - 1.0f) * norm; a2 = 0.0f;
}

void AnchorDSP::Biquad::setIdentity() noexcept
{
    b0 = 1.0f; b1 = 0.0f; b2 = 0.0f; a1 = 0.0f; a2 = 0.0f;
}

float AnchorDSP::Biquad::process(float input) noexcept
{
    const auto output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    return output;
}

void AnchorDSP::prepare(double newSampleRate, int maximumBlockSize, int channels)
{
    sampleRate = newSampleRate;
    dryBuffer.resize(static_cast<size_t>(juce::jmax(1, maximumBlockSize * channels)));
    spectraStride = static_cast<size_t>(juce::jmax(1, maximumBlockSize));
    spectraBuffer.resize(static_cast<size_t>(juce::jmax(1, channels))
                         * (spectraBands + 2) * spectraStride);
    const auto tapeDelaySamples = static_cast<size_t>(std::ceil(newSampleRate * 0.025))
                                  + static_cast<size_t>(juce::jmax(1, maximumBlockSize)) + 2;
    for (auto& delay : tapeDelay) delay.assign(tapeDelaySamples, 0.0f);
    const auto plateLineCapacity = static_cast<size_t>(std::ceil(newSampleRate * 0.09)) + 2;
    const auto preDelayCapacity = static_cast<size_t>(std::ceil(newSampleRate * 0.51)) + 2;
    for (auto& channel : plateLines)
        for (auto& line : channel) line.assign(plateLineCapacity, 0.0f);
    for (auto& delay : platePreDelay) delay.assign(preDelayCapacity, 0.0f);
    const auto limiterCapacity = static_cast<size_t>(std::ceil(newSampleRate * 0.021))
                                 + static_cast<size_t>(juce::jmax(1, maximumBlockSize)) + 2;
    for (auto& delay : limiterDelay) delay.assign(limiterCapacity, 0.0f);
    const auto orbitCapacity = static_cast<size_t>(std::ceil(newSampleRate * 6.05))
                               + static_cast<size_t>(juce::jmax(1, maximumBlockSize)) + 2;
    for (auto& delay : orbitDelay) delay.assign(orbitCapacity, 0.0f);
    for (auto& band : fluxEnvelopes)
        for (auto& envelope : band)
            envelope.prepare(newSampleRate);
    for (auto& band : prismEnvelopes)
        for (auto& envelope : band)
            envelope.prepare(newSampleRate);
    for (auto& band : spectraEnvelopes)
        for (auto& envelope : band)
            envelope.prepare(newSampleRate);
    for (auto& envelope : analogDynamicsEnvelopes) envelope.prepare(newSampleRate);
    for (auto& envelope : deesserEnvelopes) envelope.prepare(newSampleRate);
    for (auto& band : resonanceEnvelopes)
        for (auto& envelope : band) envelope.prepare(newSampleRate);
    for (auto& envelope : orbitDuckEnvelopes) envelope.prepare(newSampleRate);

    // Heritage: 20 ms gain ramps, real 2x/4x oversampling for the character
    // stage, and a dry delay so mix/bypass stay aligned with the wet path.
    for (auto* smoother : { &heritageInputGain, &heritageOutputGain, &heritageCompensation, &heritageMix, &heritageBypass })
        smoother->reset(newSampleRate, 0.02);
    plateBypass.reset(newSampleRate, 0.02);
    plateBypass.setCurrentAndTargetValue(0.0f);
    const auto oversampledChannels = static_cast<size_t>(juce::jlimit(1, static_cast<int>(maxChannels), channels));
    for (size_t stage = 0; stage < heritageOversampling.size(); ++stage)
    {
        heritageOversampling[stage] = std::make_unique<juce::dsp::Oversampling<float>>(
            oversampledChannels, stage + 1,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        heritageOversampling[stage]->initProcessing(static_cast<size_t>(juce::jmax(1, maximumBlockSize)));
    }
    heritageOversampleChoice = -1;
    heritageLatency = 0;
    for (auto& ring : heritageDryDelay) ring.assign(heritageDryCapacity, 0.0f);
    heritageDryPosition.fill(0);
    reset();
}

void AnchorDSP::reset()
{
    for (auto& channel : heritageFilters)
        for (auto& filter : channel)
            filter.reset();
    heritagePrevious.fill(0.0f);
    heritageInputGain.setCurrentAndTargetValue(heritageInputGain.getTargetValue());
    heritageOutputGain.setCurrentAndTargetValue(heritageOutputGain.getTargetValue());
    heritageCompensation.setCurrentAndTargetValue(heritageCompensation.getTargetValue());
    heritageMix.setCurrentAndTargetValue(heritageMix.getTargetValue());
    heritageBypass.setCurrentAndTargetValue(heritageBypass.getTargetValue());
    heritagePrimed = false;
    for (auto& oversampler : heritageOversampling) if (oversampler != nullptr) oversampler->reset();
    for (auto& ring : heritageDryDelay) std::fill(ring.begin(), ring.end(), 0.0f);
    heritageDryPosition.fill(0);
    for (auto& channel : prismFilters) for (auto& filter : channel) filter.reset();
    for (auto& channel : prismDetectors) for (auto& filter : channel) filter.reset();
    for (auto& band : prismEnvelopes) for (auto& envelope : band) envelope.reset();
    for (auto& channel : fluxFilters)
        for (auto& filter : channel)
            filter.reset();
    for (auto& channel : fluxDetectors)
        for (auto& filter : channel)
            filter.reset();
    for (auto& band : fluxEnvelopes)
        for (auto& envelope : band)
            envelope.reset();
    for (auto& channel : spectraLowPass)
        for (auto& crossover : channel)
            for (auto& filter : crossover)
                filter.reset();
    for (auto& channel : spectraHighPass)
        for (auto& crossover : channel)
            for (auto& filter : crossover)
                filter.reset();
    for (auto& band : spectraEnvelopes)
        for (auto& envelope : band)
            envelope.reset();
    for (auto& history : spectraLinearHistory) history.fill(0.0f);
    spectraLinearPosition.fill(0);
    for (auto& channel : ironFilters)
        for (auto& filter : channel)
            filter.reset();
    for (auto& channel : consoleFilters)
        for (auto& filter : channel)
            filter.reset();
    for (auto& channel : tapeFilters)
        for (auto& filter : channel)
            filter.reset();
    for (auto& channel : valveFilters)
        for (auto& filter : channel)
            filter.reset();
    for (auto& filter : dynamicsSidechainFilters) filter.reset();
    for (auto& envelope : analogDynamicsEnvelopes) envelope.reset();
    for (auto& channel : passiveEqFilters)
        for (auto& filter : channel) filter.reset();
    for (auto& channel : plateToneFilters)
        for (auto& filter : channel) filter.reset();
    for (auto& channel : plateLines)
        for (auto& line : channel) std::fill(line.begin(), line.end(), 0.0f);
    for (auto& channel : platePositions) channel.fill(0);
    for (auto& channel : plateDampingState) channel.fill(0.0f);
    for (auto& delay : platePreDelay) std::fill(delay.begin(), delay.end(), 0.0f);
    platePreDelayPosition.fill(0);
    for (auto& delay : limiterDelay) std::fill(delay.begin(), delay.end(), 0.0f);
    limiterPosition.fill(0); limiterGain.fill(1.0f); limiterPrevious.fill(0.0f);
    for (auto& filter : deesserDetectors) filter.reset();
    for (auto& filter : deesserFilters) filter.reset();
    for (auto& envelope : deesserEnvelopes) envelope.reset();
    for (auto& channel : resonanceDetectors) for (auto& filter : channel) filter.reset();
    for (auto& channel : resonanceFilters) for (auto& filter : channel) filter.reset();
    for (auto& band : resonanceEnvelopes) for (auto& envelope : band) envelope.reset();
    resonanceProfile.fill(0.0f); resonanceProfileValid = false;
    for (auto& delay : orbitDelay) std::fill(delay.begin(), delay.end(), 0.0f);
    orbitPosition.fill(0); orbitPhase = 0.0;
    for (auto& channel : orbitTapFilters) for (auto& filter : channel) filter.reset();
    for (auto& envelope : orbitDuckEnvelopes) envelope.reset();
    for (auto& position : tapeWritePosition) position = 0;
    for (auto& delay : tapeDelay) std::fill(delay.begin(), delay.end(), 0.0f);
    tapeWowPhase = 0.0;
    tapeFlutterPhase = 0.0;
}

float AnchorDSP::value(const juce::AudioProcessorValueTreeState& state, const juce::String& id, float fallback) const
{
    if (const auto* parameter = state.getRawParameterValue(parameterPrefix + id))
        return parameter->load(std::memory_order_relaxed);
    return fallback;
}

void AnchorDSP::applyInputAndOutput(juce::AudioBuffer<float>& buffer, float inputDb, float outputDb)
{
    buffer.applyGain(gainFromDb(inputDb + outputDb));
}

void AnchorDSP::processPrism(juce::AudioBuffer<float>& buffer, const juce::AudioProcessorValueTreeState& state)
{
    const auto channels=juce::jmin<int>(buffer.getNumChannels(),static_cast<int>(maxChannels));
    const auto samples=buffer.getNumSamples();
    buffer.applyGain(gainFromDb(value(state,"input_gain")));
    for(int channel=0;channel<channels;++channel)
        juce::FloatVectorOperations::copy(dryBuffer.data()+channel*samples,buffer.getReadPointer(channel),samples);
    bool anySolo=false;
    for(size_t band=0;band<prismBands;++band)
        anySolo=anySolo||value(state,"band."+juce::String(static_cast<int>(band)+1).paddedLeft('0',2)+".solo")>.5f;
    float staticGainSum=0.0f;
    for(size_t band=0;band<prismBands;++band)
    {
        const auto prefix="band."+juce::String(static_cast<int>(band)+1).paddedLeft('0',2);
        const auto enabled=value(state,prefix+".enabled",1)>.5f;
        const auto solo=value(state,prefix+".solo")>.5f;
        if(!enabled||(anySolo&&!solo))continue;
        const auto frequency=juce::jlimit(20.0f,static_cast<float>(sampleRate*.45),value(state,prefix+".frequency",1000));
        const auto q=juce::jlimit(.05f,50.0f,value(state,prefix+".q",1));
        const auto staticGain=value(state,prefix+".gain");
        const auto type=static_cast<int>(std::lround(value(state,prefix+".type")));
        const auto dynamicMode=static_cast<int>(std::lround(value(state,prefix+".dynamic_mode")));
        const auto stereoMode=static_cast<int>(std::lround(value(state,prefix+".stereo_mode")));
        const auto deltaEnabled=value(state,prefix+".delta")>.5f;
        // Bell and shelf filters at 0 dB are exact identity operations. Avoid running them,
        // especially across PRISM's 24 available slots.
        if(dynamicMode==0&&type<=2&&std::abs(staticGain)<1.0e-5f&&!deltaEnabled)
            continue;
        const auto midSide=channels==2&&(stereoMode==3||stereoMode==4);
        if(midSide)for(int sample=0;sample<samples;++sample){const auto l=buffer.getSample(0,sample),r=buffer.getSample(1,sample);buffer.setSample(0,sample,(l+r)*.5f);buffer.setSample(1,sample,(l-r)*.5f);}
        const auto channelActive=[stereoMode](int channel){if(stereoMode==1||stereoMode==3)return channel==0;if(stereoMode==2||stereoMode==4)return channel==1;return true;};
        float detectorLevel=0.0f;
        if(dynamicMode>0)
        {
            for(int channel=0;channel<channels;++channel)
            {
                if(!channelActive(channel))continue;
                auto&detector=prismDetectors[static_cast<size_t>(channel)][band];
                auto&envelope=prismEnvelopes[band][static_cast<size_t>(channel)];
                detector.setBandPass(sampleRate,frequency,q);
                envelope.setAttackRelease(value(state,prefix+".attack",15),value(state,prefix+".release",220));
                for(int sample=0;sample<samples;++sample)envelope.processSample(detector.process(buffer.getSample(channel,sample)));
                detectorLevel=juce::jmax(detectorLevel,envelope.current());
            }
        }
        float dynamicGain=0.0f;
        if(dynamicMode>0)
        {
            const auto levelDb=juce::Decibels::gainToDecibels(detectorLevel,-120.0f);
            const auto threshold=value(state,prefix+".threshold",-24);
            const auto range=value(state,prefix+".dynamic_range",6);
            dynamicGain=dynamicMode==1?-juce::jmin(range,juce::jmax(0.0f,levelDb-threshold)*.5f)
                                      :juce::jmin(range,juce::jmax(0.0f,threshold-levelDb)*.5f);
        }
        const auto totalGain=staticGain+dynamicGain;
        staticGainSum+=staticGain;
        for(int channel=0;channel<channels;++channel)
        {
            if(!channelActive(channel))continue;
            auto&filter=prismFilters[static_cast<size_t>(channel)][band];
            if(type==1)filter.setLowShelf(sampleRate,frequency,gainFromDb(totalGain));
            else if(type==2)filter.setHighShelf(sampleRate,frequency,gainFromDb(totalGain));
            else if(type==3)filter.setHighPass(sampleRate,frequency,q);
            else if(type==4)filter.setLowPass(sampleRate,frequency,q);
            else if(type==5)filter.setNotch(sampleRate,frequency,q);
            else if(type==6)filter.setBandPass(sampleRate,frequency,q);
            else if(type==7)filter.setPeak(sampleRate,frequency,q,gainFromDb(totalGain*.5f));
            else filter.setPeak(sampleRate,frequency,q,gainFromDb(totalGain));
            auto*output=buffer.getWritePointer(channel);
            for(int sample=0;sample<samples;++sample){const auto before=output[sample];const auto after=filter.process(before);output[sample]=deltaEnabled?after-before:after;}
        }
        if(midSide)for(int sample=0;sample<samples;++sample){const auto m=buffer.getSample(0,sample),s=buffer.getSample(1,sample);buffer.setSample(0,sample,m+s);buffer.setSample(1,sample,m-s);}
    }

    const auto drive = gainFromDb(value(state, "color_drive"));
    const auto amount = value(state, "color_amount") * 0.01f;
    const auto colorMode=static_cast<int>(std::lround(value(state,"color_mode")));
    if (amount > 0.0f)
        for (int channel = 0; channel < channels; ++channel)
        {
            auto* output = buffer.getWritePointer(channel);
            for (int sample = 0; sample < samples; ++sample)
            {
                const auto dry = output[sample],driven=dry*drive;
                const auto wet=colorMode==1?std::tanh(driven+.04f*driven*driven)
                              :colorMode==2?static_cast<float>(std::atan(driven)*2.0/juce::MathConstants<double>::pi)
                              :colorMode==3?std::tanh(driven*1.35f)/1.15f:std::tanh(driven);
                output[sample] = juce::jmap(amount, dry, wet/juce::jmax(1.0f,drive*.72f));
            }
        }
    const auto automaticGain=value(state,"auto_gain",1)>.5f?-staticGainSum*.35f:0.0f;
    buffer.applyGain(gainFromDb(value(state,"output_gain")+automaticGain));
}

void AnchorDSP::processFlux(juce::AudioBuffer<float>& buffer, const juce::AudioProcessorValueTreeState& state)
{
    const auto channelCount = juce::jmin<int>(buffer.getNumChannels(), static_cast<int>(maxChannels));
    for (int channel = 0; channel < channelCount; ++channel)
        juce::FloatVectorOperations::copy(dryBuffer.data() + channel * buffer.getNumSamples(),
                                          buffer.getReadPointer(channel), buffer.getNumSamples());
    buffer.applyGain(gainFromDb(value(state, "input_gain")));
    const auto activeBands = juce::jlimit(1, static_cast<int>(fluxBands),
                                         static_cast<int>(std::lround(value(state, "band_count", 4.0f))));
    const auto useExternalSidechain = value(state, "external_sc") > 0.5f
                                      && buffer.getNumChannels() >= channelCount * 2;
    const auto adaptive = value(state, "adaptive", 1.0f) > 0.5f;
    const auto autoLearn = value(state, "auto_learn") > 0.5f;
    bool listening = false;

    for (int band = 0; band < activeBands; ++band)
    {
        const auto prefix = "band." + juce::String(band + 1).paddedLeft('0', 2);
        const auto frequency = juce::jlimit(20.0f, static_cast<float>(sampleRate * 0.45),
                                            value(state, prefix + ".frequency", 1000.0f));
        const auto q = juce::jlimit(0.1f, 24.0f, value(state, prefix + ".q", 1.2f));
        const auto attack = value(state, prefix + ".attack", 20.0f);
        const auto release = value(state, prefix + ".release", 250.0f);
        const auto stereoMode = static_cast<int>(std::lround(value(state, prefix + ".stereo_mode")));
        const auto midSide = channelCount == 2 && (stereoMode == 3 || stereoMode == 4);
        if (midSide)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto left = buffer.getSample(0, sample), right = buffer.getSample(1, sample);
                buffer.setSample(0, sample, (left + right) * .5f);
                buffer.setSample(1, sample, (left - right) * .5f);
            }
        const auto channelActive = [stereoMode](int channel)
        {
            if (stereoMode == 1 || stereoMode == 3) return channel == 0;
            if (stereoMode == 2 || stereoMode == 4) return channel == 1;
            return true;
        };
        float linkedEnvelope = 0.0f;

        for (int channel = 0; channel < channelCount; ++channel)
        {
            if (!channelActive(channel)) continue;
            auto& detector = fluxDetectors[static_cast<size_t>(channel)][static_cast<size_t>(band)];
            auto& envelope = fluxEnvelopes[static_cast<size_t>(band)][static_cast<size_t>(channel)];
            detector.setBandPass(sampleRate, frequency, q);
            envelope.setAttackRelease(attack, release);
            const auto detectorChannel = useExternalSidechain ? channel + channelCount : channel;
            const auto* samples = buffer.getReadPointer(detectorChannel);
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                envelope.processSample(detector.process(samples[sample]));
            linkedEnvelope = juce::jmax(linkedEnvelope, envelope.current());
        }

        const auto levelDb = juce::Decibels::gainToDecibels(linkedEnvelope, -120.0f);
        auto threshold = value(state, prefix + ".threshold", -24.0f);
        if (autoLearn) threshold = levelDb - 6.0f;
        else if (adaptive) threshold += juce::jlimit(-12.0f, 12.0f, (levelDb - threshold) * 0.2f);
        const auto ratio = value(state, prefix + ".ratio", 2.0f);
        const auto requestedRange = value(state, prefix + ".range", -4.0f);
        const auto upward = value(state, prefix + ".direction") >= 0.5f;
        const auto range = upward ? std::abs(requestedRange) : -std::abs(requestedRange);
        const auto mode = upward ? DynamicsGainComputer::Mode::expandUp
                                 : DynamicsGainComputer::Mode::compress;
        const auto dynamicDb = DynamicsGainComputer::gainDecibels(levelDb, threshold, ratio, range, mode);
        const auto bandListen = value(state, prefix + ".listen") > 0.5f;
        listening = listening || bandListen;

        for (int channel = 0; channel < channelCount; ++channel)
        {
            if (!channelActive(channel)) continue;
            auto& filter = fluxFilters[static_cast<size_t>(channel)][static_cast<size_t>(band)];
            filter.setPeak(sampleRate, frequency, q, gainFromDb(dynamicDb));
            auto* samples = buffer.getWritePointer(channel);
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto before = samples[sample];
                const auto processed = filter.process(before);
                samples[sample] = bandListen ? before - processed : processed;
            }
        }
        if (midSide)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto mid = buffer.getSample(0, sample), side = buffer.getSample(1, sample);
                buffer.setSample(0, sample, mid + side);
                buffer.setSample(1, sample, mid - side);
            }
    }

    buffer.applyGain(gainFromDb(value(state, "output_gain")));
    if (value(state, "delta") > 0.5f && !listening)
        for (int channel = 0; channel < channelCount; ++channel)
        {
            auto* output = buffer.getWritePointer(channel);
            const auto* dry = dryBuffer.data() + channel * buffer.getNumSamples();
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample) output[sample] = dry[sample] - output[sample];
        }
}

void AnchorDSP::processSpectra(juce::AudioBuffer<float>& buffer, const juce::AudioProcessorValueTreeState& state)
{
    const auto samples = buffer.getNumSamples();
    const auto channels = juce::jmin<int>(buffer.getNumChannels(), static_cast<int>(maxChannels));
    const auto bandCount = juce::jlimit(2, static_cast<int>(spectraBands),
                                       static_cast<int>(std::lround(value(state, "band_count", 4.0f))));
    const auto required = static_cast<size_t>(channels) * (spectraBands + 2) * spectraStride;
    jassert(spectraStride >= static_cast<size_t>(samples) && spectraBuffer.size() >= required);
    if (spectraStride < static_cast<size_t>(samples) || spectraBuffer.size() < required)
        return;

    const auto scratch = [this](int channel, size_t slot)
    {
        return spectraBuffer.data() + (static_cast<size_t>(channel) * (spectraBands + 2) + slot) * spectraStride;
    };

    for (int channel = 0; channel < channels; ++channel)
    {
        juce::FloatVectorOperations::copy(dryBuffer.data() + channel * samples,
                                          buffer.getReadPointer(channel), samples);
        juce::FloatVectorOperations::copy(scratch(channel, spectraBands),
                                          buffer.getReadPointer(channel), samples);
    }

    std::array<float, spectraCrossovers> crossoverFrequencies {};
    float previousFrequency = 20.0f;
    for (int crossover = 0; crossover < bandCount - 1; ++crossover)
    {
        const auto prefix = "xover." + juce::String(crossover + 1).paddedLeft('0', 2) + ".frequency";
        const auto maximum = static_cast<float>(sampleRate * 0.45);
        const auto lowerBound = juce::jmin(maximum, previousFrequency * 1.25f);
        const auto frequency = juce::jlimit(lowerBound, maximum,
                                            value(state, prefix, 1000.0f));
        crossoverFrequencies[static_cast<size_t>(crossover)] = frequency;
        previousFrequency = frequency;
    }

    const auto linearPhase = value(state, "split_phase") > 0.5f;
    if (linearPhase)
    {
        constexpr auto delay = (spectraLinearTaps - 1) / 2;
        std::array<std::array<float, spectraLinearTaps>, spectraCrossovers> coefficients {};
        for (int crossover = 0; crossover < bandCount - 1; ++crossover)
        {
            auto& taps = coefficients[static_cast<size_t>(crossover)];
            const auto normalisedCutoff = crossoverFrequencies[static_cast<size_t>(crossover)] / static_cast<float>(sampleRate);
            float sum = 0.0f;
            for (size_t tap = 0; tap < spectraLinearTaps; ++tap)
            {
                const auto offset = static_cast<double>(tap) - static_cast<double>(delay);
                const auto sinc = offset == 0.0 ? 2.0 * normalisedCutoff
                    : std::sin(2.0 * juce::MathConstants<double>::pi * normalisedCutoff * offset)
                      / (juce::MathConstants<double>::pi * offset);
                const auto window = 0.5 - 0.5 * std::cos(juce::MathConstants<double>::twoPi
                                                         * static_cast<double>(tap)
                                                         / static_cast<double>(spectraLinearTaps - 1));
                taps[tap] = static_cast<float>(sinc * window);
                sum += taps[tap];
            }
            if (std::abs(sum) > 1.0e-9f)
                for (auto& tap : taps) tap /= sum;
        }

        for (int channel = 0; channel < channels; ++channel)
        {
            auto& history = spectraLinearHistory[static_cast<size_t>(channel)];
            auto& position = spectraLinearPosition[static_cast<size_t>(channel)];
            const auto* source = scratch(channel, spectraBands);
            auto* alignedDry = scratch(channel, spectraBands + 1);
            for (int sample = 0; sample < samples; ++sample)
            {
                history[position] = source[sample];
                alignedDry[sample] = history[(position + spectraLinearTaps - delay) % spectraLinearTaps];
                std::array<float, spectraCrossovers> lowpasses {};
                for (int crossover = 0; crossover < bandCount - 1; ++crossover)
                    for (size_t tap = 0; tap < spectraLinearTaps; ++tap)
                        lowpasses[static_cast<size_t>(crossover)]
                            += coefficients[static_cast<size_t>(crossover)][tap]
                               * history[(position + spectraLinearTaps - tap) % spectraLinearTaps];
                scratch(channel, 0)[sample] = lowpasses[0];
                for (int band = 1; band < bandCount - 1; ++band)
                    scratch(channel, static_cast<size_t>(band))[sample]
                        = lowpasses[static_cast<size_t>(band)] - lowpasses[static_cast<size_t>(band - 1)];
                scratch(channel, static_cast<size_t>(bandCount - 1))[sample]
                    = alignedDry[sample] - lowpasses[static_cast<size_t>(bandCount - 2)];
                position = (position + 1) % spectraLinearTaps;
            }
        }
    }
    else
    {
        for (int crossover = 0; crossover < bandCount - 1; ++crossover)
        {
            const auto frequency = crossoverFrequencies[static_cast<size_t>(crossover)];
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto sourceSlot = spectraBands + static_cast<size_t>(crossover & 1);
                const auto destinationSlot = spectraBands + static_cast<size_t>((crossover + 1) & 1);
                const auto* source = scratch(channel, sourceSlot);
                auto* low = scratch(channel, static_cast<size_t>(crossover));
                auto* high = scratch(channel, destinationSlot);
                auto& lowFilters = spectraLowPass[static_cast<size_t>(channel)][static_cast<size_t>(crossover)];
                auto& highFilters = spectraHighPass[static_cast<size_t>(channel)][static_cast<size_t>(crossover)];
                for (auto& filter : lowFilters) filter.setLowPass(sampleRate, frequency);
                for (auto& filter : highFilters) filter.setHighPass(sampleRate, frequency);
                for (int sample = 0; sample < samples; ++sample)
                {
                    auto lowSample = source[sample], highSample = source[sample];
                    for (auto& filter : lowFilters) lowSample = filter.process(lowSample);
                    for (auto& filter : highFilters) highSample = filter.process(highSample);
                    low[sample] = lowSample;
                    high[sample] = highSample;
                }
            }
        }
        const auto finalRemainder = spectraBands + static_cast<size_t>((bandCount - 1) & 1);
        for (int channel = 0; channel < channels; ++channel)
            juce::FloatVectorOperations::copy(scratch(channel, static_cast<size_t>(bandCount - 1)),
                                              scratch(channel, finalRemainder), samples);
    }

    std::array<float, spectraBands> gainDb {};
    std::array<bool, spectraBands> bypass {};
    std::array<bool, spectraBands> solo {};
    std::array<bool, spectraBands> delta {};
    float gainSum = 0.0f;
    int linkedBands = 0;
    bool anySolo = false;

    for (int band = 0; band < bandCount; ++band)
    {
        const auto prefix = "band." + juce::String(band + 1).paddedLeft('0', 2);
        bypass[static_cast<size_t>(band)] = value(state, prefix + ".bypass") > 0.5f;
        solo[static_cast<size_t>(band)] = value(state, prefix + ".solo") > 0.5f;
        delta[static_cast<size_t>(band)] = value(state, prefix + ".delta") > 0.5f;
        anySolo = anySolo || solo[static_cast<size_t>(band)];
        if (bypass[static_cast<size_t>(band)]) continue;

        float linkedEnvelope = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            auto& envelope = spectraEnvelopes[static_cast<size_t>(band)][static_cast<size_t>(channel)];
            envelope.setAttackRelease(value(state, prefix + ".attack", 20.0f),
                                      value(state, prefix + ".release", 250.0f));
            const auto* bandSamples = scratch(channel, static_cast<size_t>(band));
            for (int sample = 0; sample < samples; ++sample)
                envelope.processSample(bandSamples[sample]);
            linkedEnvelope = juce::jmax(linkedEnvelope, envelope.current());
        }

        const auto modeIndex = static_cast<int>(std::lround(value(state, prefix + ".mode")));
        const auto mode = modeIndex == 1 ? DynamicsGainComputer::Mode::expandUp
                         : modeIndex == 2 ? DynamicsGainComputer::Mode::expandDown
                                          : DynamicsGainComputer::Mode::compress;
        const auto requestedRange = value(state, prefix + ".range", -6.0f);
        const auto range = mode == DynamicsGainComputer::Mode::expandUp
                               ? std::abs(requestedRange) : -std::abs(requestedRange);
        gainDb[static_cast<size_t>(band)] = DynamicsGainComputer::gainDecibels(
            juce::Decibels::gainToDecibels(linkedEnvelope, -120.0f),
            value(state, prefix + ".threshold", -24.0f),
            value(state, prefix + ".ratio", 3.0f), range, mode);
        gainSum += gainDb[static_cast<size_t>(band)];
        ++linkedBands;
    }

    const auto link = juce::jlimit(0.0f, 1.0f, value(state, "global_link", 100.0f) * 0.01f);
    const auto linkedGain = linkedBands > 0 ? gainSum / static_cast<float>(linkedBands) : 0.0f;
    const auto mix = juce::jlimit(0.0f, 1.0f, value(state, "global_mix", 100.0f) * 0.01f);

    for (int channel = 0; channel < channels; ++channel)
    {
        auto* output = buffer.getWritePointer(channel);
        juce::FloatVectorOperations::clear(output, samples);
        for (int band = 0; band < bandCount; ++band)
        {
            if (anySolo && !solo[static_cast<size_t>(band)]) continue;
            const auto prefix = "band." + juce::String(band + 1).paddedLeft('0', 2);
            const auto dynamics = bypass[static_cast<size_t>(band)]
                                      ? 0.0f
                                      : juce::jmap(link, gainDb[static_cast<size_t>(band)], linkedGain);
            const auto totalGain = bypass[static_cast<size_t>(band)]
                                       ? 1.0f
                                       : gainFromDb(dynamics + value(state, prefix + ".makeup"));
            const auto* bandSamples = scratch(channel, static_cast<size_t>(band));
            for (int sample = 0; sample < samples; ++sample)
            {
                const auto wet = delta[static_cast<size_t>(band)]
                                     ? bandSamples[sample] * (totalGain - 1.0f)
                                     : bandSamples[sample] * totalGain;
                output[sample] += wet;
            }
        }

        const auto* dry = linearPhase ? scratch(channel, spectraBands + 1)
                                      : dryBuffer.data() + channel * samples;
        for (int sample = 0; sample < samples; ++sample)
            output[sample] = dry[sample] + mix * (output[sample] - dry[sample]);
    }
}

void AnchorDSP::processHeritage(juce::AudioBuffer<float>& buffer, const juce::AudioProcessorValueTreeState& state)
{
    // Signal flow per the build specification:
    // input trim -> HPF/LPF -> five bands -> analog character -> auto gain -> mix -> output trim.
    const auto samplesPerChannel = buffer.getNumSamples();
    const auto channelCount = buffer.getNumChannels();
    const auto channels = juce::jmin<int>(channelCount, static_cast<int>(maxChannels));
    if (samplesPerChannel <= 0 || channels <= 0) return;

    // ---- control-rate reads
    const auto inputDb = value(state, "input_trim");
    const auto outputDb = value(state, "output_trim");
    const auto mixTarget = juce::jlimit(0.0f, 1.0f, value(state, "mix", 100.0f) * 0.01f);
    const auto bypassTarget = value(state, "bypass", 0.0f) > 0.5f ? 1.0f : 0.0f;
    const auto invert = value(state, "phase", 0.0f) > 0.5f;
    const auto character = juce::jlimit(0, 3, static_cast<int>(std::lround(value(state, "character"))));
    const auto drive = juce::jlimit(0.0f, 10.0f, value(state, "drive"));
    const auto oversampleChoice = juce::jlimit(0, 2, static_cast<int>(std::lround(value(state, "oversampling", 1.0f))));
    const auto midSide = value(state, "stereo_mode") > 0.5f && channels >= 2;
    const auto eqEngaged = value(state, "eq_in", 1.0f) > 0.5f;
    const auto filtersEngaged = value(state, "filter_in", 1.0f) > 0.5f;
    const auto steepFilters = value(state, "filter_slope", 0.0f) > 0.5f;   // 0 = 18 dB/oct, 1 = 24 dB/oct
    const auto autoGain = value(state, "auto_gain", 1.0f) > 0.5f;

    heritageInputGain.setTargetValue(gainFromDb(inputDb) * (invert ? -1.0f : 1.0f));
    heritageMix.setTargetValue(mixTarget);
    heritageBypass.setTargetValue(bypassTarget);
    if (! heritagePrimed)
    {
        heritageInputGain.setCurrentAndTargetValue(heritageInputGain.getTargetValue());
        heritageMix.setCurrentAndTargetValue(mixTarget);
        heritageBypass.setCurrentAndTargetValue(bypassTarget);
    }

    // ---- latency follows the oversampler in use, and the dry path is delayed
    //      to match so mix and bypass never comb against the wet signal.
    auto* oversampler = oversampleChoice > 0 ? heritageOversampling[static_cast<size_t>(oversampleChoice - 1)].get()
                                             : nullptr;
    if (oversampleChoice != heritageOversampleChoice)
    {
        heritageOversampleChoice = oversampleChoice;
        if (oversampler != nullptr) oversampler->reset();
    }
    heritageLatency = oversampler != nullptr ? static_cast<int>(std::lround(oversampler->getLatencyInSamples())) : 0;
    const auto dryDelay = juce::jlimit(0, static_cast<int>(heritageDryCapacity) - 1, heritageLatency);

    // ---- capture the raw input, delayed by the reported latency
    const auto dryAvailable = dryBuffer.size() >= static_cast<size_t>(samplesPerChannel * channelCount);
    jassert(dryAvailable);
    if (dryAvailable)
        for (int channel = 0; channel < channels; ++channel)
        {
            auto& ring = heritageDryDelay[static_cast<size_t>(channel)];
            auto& position = heritageDryPosition[static_cast<size_t>(channel)];
            const auto* in = buffer.getReadPointer(channel);
            auto* dry = dryBuffer.data() + channel * samplesPerChannel;
            for (int sample = 0; sample < samplesPerChannel; ++sample)
            {
                ring[position] = in[sample];
                const auto readIndex = (position + heritageDryCapacity - static_cast<size_t>(dryDelay)) % heritageDryCapacity;
                dry[sample] = ring[readIndex];
                position = (position + 1) % heritageDryCapacity;
            }
        }

    // ---- input trim and polarity, smoothed
    for (int sample = 0; sample < samplesPerChannel; ++sample)
    {
        const auto gain = heritageInputGain.getNextValue();
        for (int channel = 0; channel < channels; ++channel)
            buffer.getWritePointer(channel)[sample] *= gain;
    }

    // ---- L/R -> M/S
    if (midSide)
        for (int sample = 0; sample < samplesPerChannel; ++sample)
        {
            const auto left = buffer.getSample(0, sample);
            const auto right = buffer.getSample(1, sample);
            buffer.setSample(0, sample, (left + right) * 0.70710678f);
            buffer.setSample(1, sample, (left - right) * 0.70710678f);
        }

    // ---- filters and bands
    const std::array<juce::String, 5> names { "low", "lowmid", "mid", "highmid", "high" };
    const auto nyquistGuard = static_cast<float>(sampleRate * 0.45);
    const auto hpfHz = juce::jlimit(20.0f, nyquistGuard, value(state, "hpf", 20.0f));
    const auto lpfHz = juce::jlimit(100.0f, nyquistGuard, value(state, "lpf", 40000.0f));
    // the HPF legend reads OFF at its minimum, and an LPF above the audio band is off
    const auto hpfActive = filtersEngaged && hpfHz > 20.5f;
    const auto lpfActive = filtersEngaged && value(state, "lpf", 40000.0f) < 39000.0f;

    auto boostSum = 0.0f;
    if (eqEngaged)
        for (const auto& name : names)
            if (value(state, "band." + name + ".enabled", 1.0f) > 0.5f)
                boostSum += juce::jmax(0.0f, value(state, "band." + name + ".gain", 0.0f));

    // slot map: 0/7 high-pass stages, 1..5 bands, 6/8 low-pass stages
    static constexpr std::array<size_t, 9> chainOrder { 0, 7, 1, 2, 3, 4, 5, 6, 8 };

    for (int channel = 0; channel < channels; ++channel)
    {
        auto& filters = heritageFilters[static_cast<size_t>(channel)];

        if (hpfActive)
        {
            if (steepFilters)
            {
                // 4th-order Butterworth: two biquads with the classic Q pair
                filters[0].setHighPass(sampleRate, hpfHz, 0.54119610f);
                filters[7].setHighPass(sampleRate, hpfHz, 1.30656296f);
            }
            else
            {
                // 3rd-order: one biquad plus a first-order section
                filters[0].setHighPass(sampleRate, hpfHz, 0.70710678f);
                filters[7].setFirstOrderHighPass(sampleRate, hpfHz);
            }
        }
        else { filters[0].setIdentity(); filters[7].setIdentity(); }

        if (lpfActive)
        {
            if (steepFilters)
            {
                filters[6].setLowPass(sampleRate, lpfHz, 0.54119610f);
                filters[8].setLowPass(sampleRate, lpfHz, 1.30656296f);
            }
            else
            {
                filters[6].setLowPass(sampleRate, lpfHz, 0.70710678f);
                filters[8].setFirstOrderLowPass(sampleRate, lpfHz);
            }
        }
        else { filters[6].setIdentity(); filters[8].setIdentity(); }

        for (size_t band = 0; band < names.size(); ++band)
        {
            const auto prefix = "band." + names[band];
            const auto engaged = eqEngaged && value(state, prefix + ".enabled", 1.0f) > 0.5f;
            auto& filter = filters[band + 1];
            if (! engaged) { filter.setIdentity(); continue; }

            const auto frequency = juce::jlimit(20.0f, nyquistGuard, value(state, prefix + ".freq", 1000.0f));
            const auto gain = gainFromDb(value(state, prefix + ".gain", 0.0f));
            // LOW and HIGH are shelves, as printed on the faceplate. Their Q and
            // SLOPE knobs set the shelf slope: 1.0 is the steepest monotonic
            // curve, below it the transition softens, above it the shelf gains a
            // resonant corner in the classic passive-EQ manner.
            if (names[band] == "low")
                filter.setLowShelf(sampleRate, frequency, gain,
                                   juce::jlimit(0.3f, 3.0f, value(state, prefix + ".q", 0.7f)));
            else if (names[band] == "high")
                filter.setHighShelf(sampleRate, frequency, gain,
                                    juce::jlimit(0.3f, 3.0f, value(state, prefix + ".slope", 0.7f)));
            else
                filter.setPeak(sampleRate, frequency,
                               juce::jlimit(0.3f, 3.0f, value(state, prefix + ".q", 0.9f)), gain);
        }

        auto* samples = buffer.getWritePointer(channel);
        for (int sample = 0; sample < samplesPerChannel; ++sample)
        {
            auto processed = samples[sample];
            for (const auto index : chainOrder)
                processed = filters[index].process(processed);
            samples[sample] = processed;
        }
    }

    // ---- analog character, run at the oversampled rate when selected. The
    //      oversampler is always traversed when engaged so the reported latency
    //      stays constant whether or not the shaper is currently active.
    const auto shaping = character != 0 && drive > 0.0f;
    const auto driveAmount = drive * 0.1f;
    const auto driveGain = 1.0f + driveAmount * (character == 1 ? 2.4f : character == 2 ? 3.6f : 2.8f);
    const auto shape = [character, driveAmount, driveGain](float sample)
    {
        if (character == 1)
        {
            const auto bias = 0.018f * driveAmount;
            return (std::tanh(sample * driveGain + bias) - std::tanh(bias))
                 / juce::jmax(0.1f, std::tanh(driveGain));
        }
        if (character == 2)
        {
            const auto evenHarmonic = 0.16f * driveAmount * sample * sample;
            return std::tanh(sample * driveGain + evenHarmonic) / juce::jmax(0.1f, std::tanh(driveGain));
        }
        return std::atan(sample * driveGain * 1.35f) / juce::jmax(0.1f, std::atan(driveGain * 1.35f));
    };
    const auto maximumStep = 0.12f + (1.0f - driveAmount) * 0.38f;

    auto shapeChannel = [&](float* data, size_t length, int channel)
    {
        if (! shaping) return;
        auto& previous = heritagePrevious[static_cast<size_t>(channel)];
        for (size_t i = 0; i < length; ++i)
        {
            auto x = data[i];
            if (character == 3)   // tape: gentle slew limiting before the curve
                x = previous + juce::jlimit(-maximumStep, maximumStep, x - previous);
            previous = x;
            data[i] = shape(x);
        }
    };

    if (oversampler != nullptr)
    {
        juce::dsp::AudioBlock<float> block(buffer.getArrayOfWritePointers(),
                                           static_cast<size_t>(channels),
                                           static_cast<size_t>(samplesPerChannel));
        auto up = oversampler->processSamplesUp(block);
        for (int channel = 0; channel < channels; ++channel)
            shapeChannel(up.getChannelPointer(static_cast<size_t>(channel)), up.getNumSamples(), channel);
        oversampler->processSamplesDown(block);
    }
    else
    {
        for (int channel = 0; channel < channels; ++channel)
            shapeChannel(buffer.getWritePointer(channel), static_cast<size_t>(samplesPerChannel), channel);
    }

    // ---- M/S -> L/R
    if (midSide)
        for (int sample = 0; sample < samplesPerChannel; ++sample)
        {
            const auto mid = buffer.getSample(0, sample);
            const auto side = buffer.getSample(1, sample);
            buffer.setSample(0, sample, (mid + side) * 0.70710678f);
            buffer.setSample(1, sample, (mid - side) * 0.70710678f);
        }

    // ---- auto gain -> mix -> output trim -> bypass crossfade, all smoothed
    const auto compensationDb = autoGain ? -(boostSum * 0.12f + (character == 0 ? 0.0f : drive * 0.16f)) : 0.0f;
    // Auto gain rides the wet path only, so MIX 0 % is the untouched input.
    heritageOutputGain.setTargetValue(gainFromDb(outputDb));
    heritageCompensation.setTargetValue(gainFromDb(compensationDb));
    if (! heritagePrimed)
    {
        heritageOutputGain.setCurrentAndTargetValue(heritageOutputGain.getTargetValue());
        heritageCompensation.setCurrentAndTargetValue(heritageCompensation.getTargetValue());
        heritagePrimed = true;
    }

    for (int sample = 0; sample < samplesPerChannel; ++sample)
    {
        const auto mix = heritageMix.getNextValue();
        const auto out = heritageOutputGain.getNextValue();
        const auto compensation = heritageCompensation.getNextValue();
        const auto bypass = heritageBypass.getNextValue();
        for (int channel = 0; channel < channels; ++channel)
        {
            auto* wet = buffer.getWritePointer(channel);
            const auto dry = dryAvailable ? dryBuffer[static_cast<size_t>(channel * samplesPerChannel + sample)]
                                          : wet[sample];
            auto y = dry + mix * (wet[sample] * compensation - dry);
            y *= out;
            wet[sample] = y + bypass * (dry - y);
        }
    }
}

void AnchorDSP::processIron(juce::AudioBuffer<float>& buffer, const juce::AudioProcessorValueTreeState& state)
{
    const auto channels = juce::jmin<int>(buffer.getNumChannels(), static_cast<int>(maxChannels));
    const auto transformer = juce::jlimit(0, 2, static_cast<int>(std::lround(value(state, "transformer"))));
    const auto impedance = juce::jlimit(0, 2, static_cast<int>(std::lround(value(state, "impedance", 1.0f))));
    const auto inputDb = value(state, "input") - (value(state, "pad") > 0.5f ? 20.0f : 0.0f);
    const auto drive = value(state, "drive", 25.0f) * 0.01f;
    const auto saturation = value(state, "saturation", 20.0f) * 0.01f;
    const auto driveGain = gainFromDb(inputDb) * (1.0f + drive * (3.0f + transformer));
    const std::array<float, 3> biases { 0.015f, -0.025f, 0.045f };
    const std::array<float, 3> compensation { 0.98f, 0.94f, 0.90f };
    const auto bias = biases[static_cast<size_t>(transformer)] * saturation;
    const auto phase = value(state, "phase") > 0.5f ? -1.0f : 1.0f;
    const auto impedanceTilt = static_cast<float>(impedance - 1) * 0.75f;

    for (int channel = 0; channel < channels; ++channel)
    {
        auto& filters = ironFilters[static_cast<size_t>(channel)];
        if (value(state, "hpf_in", 1.0f) > 0.5f)
            filters[0].setHighPass(sampleRate, juce::jlimit(20.0f, 320.0f, value(state, "hpf", 50.0f)));
        else
            filters[0].setIdentity();
        filters[1].setLowShelf(sampleRate, 120.0f,
                               gainFromDb(value(state, "low_tone") + impedanceTilt));
        filters[2].setHighShelf(sampleRate, 7000.0f,
                                gainFromDb(value(state, "high_tone") - impedanceTilt));
        auto* samples = buffer.getWritePointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            auto processed = filters[0].process(samples[sample] * driveGain);
            processed = filters[1].process(processed);
            processed = filters[2].process(processed);
            const auto saturated = std::tanh(processed + bias) - std::tanh(bias);
            samples[sample] = phase * compensation[static_cast<size_t>(transformer)]
                              * juce::jmap(saturation, processed, saturated);
        }
    }
    buffer.applyGain(gainFromDb(value(state, "output")));
}

void AnchorDSP::processConsole(juce::AudioBuffer<float>& buffer, const juce::AudioProcessorValueTreeState& state)
{
    const auto samplesPerChannel = buffer.getNumSamples();
    const auto channels = juce::jmin<int>(buffer.getNumChannels(), static_cast<int>(maxChannels));
    for (int channel = 0; channel < channels; ++channel)
        juce::FloatVectorOperations::copy(dryBuffer.data() + channel * samplesPerChannel,
                                          buffer.getReadPointer(channel), samplesPerChannel);

    const auto midSide = channels == 2 && value(state, "stereo_mode") >= 1.5f;
    if (midSide)
        for (int sample = 0; sample < samplesPerChannel; ++sample)
        {
            const auto left = buffer.getSample(0, sample);
            const auto right = buffer.getSample(1, sample);
            buffer.setSample(0, sample, (left + right) * 0.5f);
            buffer.setSample(1, sample, (left - right) * 0.5f);
        }

    const auto drive = value(state, "drive", 20.0f) * 0.01f;
    const auto color = value(state, "bus_color", 20.0f) * 0.01f;
    const auto inputGain = gainFromDb(value(state, "input")) * (1.0f + drive * 4.0f);
    const auto noiseEnabled = value(state, "noise") > 0.5f;
    for (int channel = 0; channel < channels; ++channel)
    {
        auto& filters = consoleFilters[static_cast<size_t>(channel)];
        if (value(state, "hpf_in", 1.0f) > 0.5f)
            filters[0].setHighPass(sampleRate, juce::jlimit(20.0f, 300.0f, value(state, "hpf", 40.0f)));
        else
            filters[0].setIdentity();
        if (value(state, "eq_in", 1.0f) > 0.5f)
        {
            filters[1].setLowShelf(sampleRate, 120.0f, gainFromDb(value(state, "low")));
            filters[2].setPeak(sampleRate,
                               juce::jlimit(100.0f, 8000.0f, value(state, "mid_freq", 1200.0f)),
                               0.85f, gainFromDb(value(state, "mid")));
            filters[3].setHighShelf(sampleRate, 9000.0f, gainFromDb(value(state, "high")));
        }
        else
            for (size_t stage = 1; stage < 4; ++stage) filters[stage].setIdentity();
        auto* samples = buffer.getWritePointer(channel);
        auto& random = noiseState[static_cast<size_t>(channel)];
        for (int sample = 0; sample < samplesPerChannel; ++sample)
        {
            auto processed = samples[sample] * inputGain;
            for (auto& filter : filters) processed = filter.process(processed);
            const auto saturated = std::tanh(processed * (1.0f + color * 2.5f))
                                   / (1.0f + color * 0.8f);
            processed = juce::jmap(juce::jlimit(0.0f, 1.0f, drive + color * 0.6f), processed, saturated);
            if (noiseEnabled)
            {
                random = random * 1664525u + 1013904223u;
                processed += (static_cast<float>(random >> 8) / 8388607.5f - 1.0f) * 0.00002f;
            }
            samples[sample] = processed;
        }
    }

    if (midSide)
        for (int sample = 0; sample < samplesPerChannel; ++sample)
        {
            const auto mid = buffer.getSample(0, sample);
            const auto side = buffer.getSample(1, sample);
            buffer.setSample(0, sample, mid + side);
            buffer.setSample(1, sample, mid - side);
        }

    const auto mix = juce::jlimit(0.0f, 1.0f, value(state, "mix", 100.0f) * 0.01f);
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* samples = buffer.getWritePointer(channel);
        const auto* dry = dryBuffer.data() + channel * samplesPerChannel;
        for (int sample = 0; sample < samplesPerChannel; ++sample)
            samples[sample] = dry[sample] + mix * (samples[sample] - dry[sample]);
    }
    buffer.applyGain(gainFromDb(value(state, "output")));
}

void AnchorDSP::processTape(juce::AudioBuffer<float>& buffer, const juce::AudioProcessorValueTreeState& state)
{
    const auto samplesPerChannel = buffer.getNumSamples();
    const auto channels = juce::jmin<int>(buffer.getNumChannels(), static_cast<int>(maxChannels));
    for (int channel = 0; channel < channels; ++channel)
        juce::FloatVectorOperations::copy(dryBuffer.data() + channel * samplesPerChannel,
                                          buffer.getReadPointer(channel), samplesPerChannel);

    const auto formula = juce::jlimit(0, 2, static_cast<int>(std::lround(value(state, "formula", 2.0f))));
    const auto speed = juce::jlimit(0, 2, static_cast<int>(std::lround(value(state, "speed", 1.0f))));
    const auto saturation = value(state, "saturation", 30.0f) * 0.01f;
    const auto inputGain = gainFromDb(value(state, "input") + value(state, "bias"))
                           * (1.0f + saturation * (2.8f - 0.4f * formula));
    const auto wowDepth = value(state, "wow", 5.0f) * 0.00004f * static_cast<float>(sampleRate);
    const auto flutterDepth = value(state, "flutter", 4.0f) * 0.000006f * static_cast<float>(sampleRate);
    const std::array<float, 3> bumpFrequency { 65.0f, 90.0f, 125.0f };
    const std::array<float, 3> rolloffFrequency { 9000.0f, 13000.0f, 18000.0f };
    const auto hiss = value(state, "hiss") * 0.0000015f;

    for (int channel = 0; channel < channels; ++channel)
    {
        auto& filters = tapeFilters[static_cast<size_t>(channel)];
        filters[0].setPeak(sampleRate, bumpFrequency[static_cast<size_t>(speed)], 0.65f,
                           gainFromDb(value(state, "head_bump") * 0.045f));
        filters[1].setLowPass(sampleRate,
                              rolloffFrequency[static_cast<size_t>(speed)]
                                  * (1.0f - value(state, "hf_rolloff") * 0.006f));
        filters[2].setHighPass(sampleRate, 18.0f);
        auto& delay = tapeDelay[static_cast<size_t>(channel)];
        auto& writePosition = tapeWritePosition[static_cast<size_t>(channel)];
        auto& random = noiseState[static_cast<size_t>(channel)];
        auto* samples = buffer.getWritePointer(channel);
        for (int sample = 0; sample < samplesPerChannel; ++sample)
        {
            delay[writePosition] = samples[sample];
            const auto modulation = 1.0f + wowDepth * static_cast<float>(std::sin(tapeWowPhase))
                                    + flutterDepth * static_cast<float>(std::sin(tapeFlutterPhase));
            const auto integerDelay = static_cast<size_t>(juce::jlimit(1.0f,
                static_cast<float>(delay.size() - 2), modulation));
            const auto readPosition = (writePosition + delay.size() - integerDelay) % delay.size();
            auto processed = delay[readPosition] * inputGain;
            processed = filters[0].process(processed);
            processed = filters[1].process(processed);
            processed = filters[2].process(processed);
            const auto curve = formula == 0 ? std::tanh(processed * 0.85f)
                             : formula == 1 ? std::atan(processed * 1.4f) * 0.72f
                                            : std::tanh(processed * 1.15f);
            processed = juce::jmap(saturation, processed, curve);
            random = random * 1664525u + 1013904223u;
            processed += (static_cast<float>(random >> 8) / 8388607.5f - 1.0f) * hiss;
            samples[sample] = processed;
            writePosition = (writePosition + 1) % delay.size();
            tapeWowPhase += juce::MathConstants<double>::twoPi * 0.45 / sampleRate;
            tapeFlutterPhase += juce::MathConstants<double>::twoPi * 6.3 / sampleRate;
        }
    }

    tapeWowPhase = std::fmod(tapeWowPhase, juce::MathConstants<double>::twoPi);
    tapeFlutterPhase = std::fmod(tapeFlutterPhase, juce::MathConstants<double>::twoPi);
    const auto mix = juce::jlimit(0.0f, 1.0f, value(state, "mix", 100.0f) * 0.01f);
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* samples = buffer.getWritePointer(channel);
        const auto* dry = dryBuffer.data() + channel * samplesPerChannel;
        for (int sample = 0; sample < samplesPerChannel; ++sample)
            samples[sample] = dry[sample] + mix * (samples[sample] - dry[sample]);
    }
    buffer.applyGain(gainFromDb(value(state, "output")));
}

void AnchorDSP::processValve(juce::AudioBuffer<float>& buffer, const juce::AudioProcessorValueTreeState& state)
{
    const auto samplesPerChannel = buffer.getNumSamples();
    const auto channels = juce::jmin<int>(buffer.getNumChannels(), static_cast<int>(maxChannels));
    for (int channel = 0; channel < channels; ++channel)
        juce::FloatVectorOperations::copy(dryBuffer.data() + channel * samplesPerChannel,
                                          buffer.getReadPointer(channel), samplesPerChannel);
    const auto topology = juce::jlimit(0, 2, static_cast<int>(std::lround(value(state, "topology"))));
    const auto driveGain = gainFromDb(value(state, "drive", 6.0f));
    const auto bias = value(state, "bias") * 0.0025f;
    const auto density = value(state, "density", 50.0f) * 0.01f;
    const auto harmonic = value(state, "harmonic_balance", 25.0f) * 0.01f;
    const auto softClip = value(state, "soft_clip", 1.0f) > 0.5f;
    // BIAS is the engineer's offset; EVEN / ODD adds the stage's own asymmetry.
    const auto asymmetry = bias + (1.0f - harmonic) * 0.35f;
    const auto triodeOffset = std::tanh(asymmetry);
    const auto pentodeOffset = std::atan(asymmetry * (1.4f + density)) * 0.72f;
    for (int channel = 0; channel < channels; ++channel)
    {
        auto& filters = valveFilters[static_cast<size_t>(channel)];
        const auto tone = value(state, "tone") * 0.01f;
        filters[0].setHighShelf(sampleRate, 3200.0f,
                                gainFromDb(tone * 8.0f + value(state, "pre_emphasis") * 0.06f));
        filters[1].setHighShelf(sampleRate, 4200.0f,
                                gainFromDb(-value(state, "post_emphasis") * 0.06f));
        auto* samples = buffer.getWritePointer(channel);
        for (int sample = 0; sample < samplesPerChannel; ++sample)
        {
            auto processed = filters[0].process(samples[sample] * driveGain);
            // EVEN / ODD sets how far off centre the valve is driven. Asymmetry
            // is what makes even-order harmonics, so EVEN runs the stage
            // off-centre and ODD runs it symmetrically. It shapes every
            // topology; before, it only reached the pentode and so did nothing
            // at all in the default triode setting.
            const auto shifted = processed + asymmetry;
            const auto triode = std::tanh(shifted) - triodeOffset;
            const auto pentode = std::atan(shifted * (1.4f + density)) * 0.72f - pentodeOffset
                                 + harmonic * 0.10f * processed * std::abs(processed);
            processed = topology == 0 ? triode
                      : topology == 1 ? pentode
                                      : std::tanh((triode + pentode) * 0.9f);
            if (softClip) processed = std::tanh(processed * (1.0f + density));
            samples[sample] = filters[1].process(processed);
        }
    }
    const auto mix = juce::jlimit(0.0f, 1.0f, value(state, "mix", 100.0f) * 0.01f);
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* samples = buffer.getWritePointer(channel);
        const auto* dry = dryBuffer.data() + channel * samplesPerChannel;
        for (int sample = 0; sample < samplesPerChannel; ++sample)
            samples[sample] = dry[sample] + mix * (samples[sample] - dry[sample]);
    }
    buffer.applyGain(gainFromDb(value(state, "output")));
}

void AnchorDSP::processAnalogCompressor(juce::AudioBuffer<float>& buffer,
                                        const juce::AudioProcessorValueTreeState& state,
                                        const juce::String& pluginId)
{
    const auto samplesPerChannel = buffer.getNumSamples();
    const auto channels = juce::jmin<int>(buffer.getNumChannels(), static_cast<int>(maxChannels));
    for (int channel = 0; channel < channels; ++channel)
        juce::FloatVectorOperations::copy(dryBuffer.data() + channel * samplesPerChannel,
                                          buffer.getReadPointer(channel), samplesPerChannel);

    float threshold = -18.0f;
    float ratio = 4.0f;
    float attackMs = 10.0f;
    float releaseMs = 300.0f;
    float makeupDb = 0.0f;
    float saturation = 0.0f;
    float stereoLink = 1.0f;
    bool useExternalSidechain = false;
    if (pluginId == "A06")
    {
        const std::array<float, 6> ratios { 2.0f, 4.0f, 8.0f, 12.0f, 20.0f, 30.0f };
        ratio = ratios[static_cast<size_t>(juce::jlimit(0, 5,
                    static_cast<int>(std::lround(value(state, "ratio", 1.0f)))) )];
        attackMs = value(state, "attack", 1.0f);
        releaseMs = value(state, "release", 200.0f);
        makeupDb = value(state, "makeup");
        saturation = value(state, "saturation", 15.0f) * 0.01f;
        useExternalSidechain = value(state, "external_sc") > 0.5f
                               && buffer.getNumChannels() >= channels * 2;
        buffer.applyGain(gainFromDb(value(state, "input")));
    }
    else if (pluginId == "A07")
    {
        threshold = -6.0f - value(state, "peak_reduction", 35.0f) * 0.48f;
        ratio = 4.0f;
        attackMs = value(state, "response", 1.0f) < 0.5f ? 8.0f : 25.0f;
        releaseMs = value(state, "release", 0.6f) * 1000.0f;
        makeupDb = value(state, "gain");
        saturation = value(state, "saturation", 15.0f) * 0.01f;
        stereoLink = value(state, "stereo_link", 100.0f) * 0.01f;
    }
    else
    {
        const std::array<float, 4> ratios { 1.5f, 2.0f, 4.0f, 10.0f };
        threshold = value(state, "threshold", -18.0f)
                    + (value(state, "knee", 1.0f) > 0.5f ? 1.5f : 0.0f);
        ratio = ratios[static_cast<size_t>(juce::jlimit(0, 3,
                    static_cast<int>(std::lround(value(state, "ratio", 2.0f)))) )];
        attackMs = value(state, "attack", 10.0f);
        releaseMs = value(state, "release", 0.3f) * 1000.0f;
        if (value(state, "auto_release") > 0.5f) releaseMs = 180.0f;
        makeupDb = value(state, "makeup");
        stereoLink = value(state, "stereo_link", 100.0f) * 0.01f;
    }

    for (int channel = 0; channel < channels; ++channel)
    {
        dynamicsSidechainFilters[static_cast<size_t>(channel)].setHighPass(
            sampleRate, value(state, "sc_hpf", 80.0f));
        analogDynamicsEnvelopes[static_cast<size_t>(channel)].setAttackRelease(attackMs, releaseMs);
    }

    float deepestReduction = 0.0f;
    for (int sample = 0; sample < samplesPerChannel; ++sample)
    {
        std::array<float, maxChannels> gainDb {};
        float linkedGain = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto detectorChannel = useExternalSidechain ? channel + channels : channel;
            const auto detector = dynamicsSidechainFilters[static_cast<size_t>(channel)].process(
                buffer.getSample(detectorChannel, sample));
            const auto envelope = analogDynamicsEnvelopes[static_cast<size_t>(channel)].processSample(detector);
            gainDb[static_cast<size_t>(channel)] = DynamicsGainComputer::gainDecibels(
                juce::Decibels::gainToDecibels(envelope, -120.0f), threshold, ratio, -36.0f,
                DynamicsGainComputer::Mode::compress);
            linkedGain = juce::jmin(linkedGain, gainDb[static_cast<size_t>(channel)]);
        }
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto appliedDb = juce::jmap(juce::jlimit(0.0f, 1.0f, stereoLink),
                                              gainDb[static_cast<size_t>(channel)], linkedGain);
            deepestReduction = juce::jmin(deepestReduction, appliedDb);
            auto processed = buffer.getSample(channel, sample) * gainFromDb(appliedDb + makeupDb);
            if (saturation > 0.0f)
                processed = juce::jmap(saturation, processed, std::tanh(processed * 1.5f) / 1.2f);
            buffer.setSample(channel, sample, processed);
        }
    }

    analogGainReduction.store(deepestReduction);
    const auto mix = juce::jlimit(0.0f, 1.0f, value(state, "mix", 100.0f) * 0.01f);
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* samples = buffer.getWritePointer(channel);
        const auto* dry = dryBuffer.data() + channel * samplesPerChannel;
        for (int sample = 0; sample < samplesPerChannel; ++sample)
            samples[sample] = dry[sample] + mix * (samples[sample] - dry[sample]);
    }
    if (pluginId == "A06" || pluginId == "A07")
        buffer.applyGain(gainFromDb(value(state, "output")));
}

void AnchorDSP::processPassiveEq(juce::AudioBuffer<float>& buffer,
                                 const juce::AudioProcessorValueTreeState& state)
{
    const auto channels = juce::jmin<int>(buffer.getNumChannels(), static_cast<int>(maxChannels));
    const auto tube = value(state, "output_stage") < 0.5f;
    const auto drive = value(state, "drive", 10.0f) * 0.01f;
    for (int channel = 0; channel < channels; ++channel)
    {
        // The passive design's boost and cut are separate networks sharing one
        // frequency selector: the cut sits above the low boost and below the
        // high boost, which is why boosting and cutting at once does not
        // cancel but carves the classic curve.
        auto& filters = passiveEqFilters[static_cast<size_t>(channel)];
        const auto lowFrequency = value(state, "low_freq", 60.0f);
        const auto highFrequency = value(state, "high_freq", 10000.0f);
        filters[0].setLowShelf(sampleRate, lowFrequency, gainFromDb(value(state, "low_boost")));
        filters[1].setLowShelf(sampleRate, juce::jlimit(20.0f, 2000.0f, lowFrequency * 2.6f),
                               gainFromDb(-value(state, "low_cut")));
        filters[2].setPeak(sampleRate, value(state, "mid_freq", 1200.0f), 0.6f,
                           gainFromDb(value(state, "mid_gain")));
        filters[3].setHighShelf(sampleRate, highFrequency, gainFromDb(value(state, "high_boost")));
        filters[4].setHighShelf(sampleRate, juce::jlimit(1000.0f, 20000.0f, highFrequency * 0.55f),
                                gainFromDb(-value(state, "high_cut")));
        auto* samples = buffer.getWritePointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            auto processed = samples[sample] * (1.0f + drive * 2.0f);
            for (auto& filter : filters) processed = filter.process(processed);
            const auto colored = tube ? std::tanh(processed * 1.15f) / 1.05f
                                      : std::atan(processed * 1.1f) * 0.92f;
            samples[sample] = juce::jmap(drive, processed, colored);
        }
    }
    buffer.applyGain(gainFromDb(value(state, "output")));
}

void AnchorDSP::processPlate(juce::AudioBuffer<float>& buffer,
                             const juce::AudioProcessorValueTreeState& state)
{
    const auto samplesPerChannel = buffer.getNumSamples();
    const auto channels = juce::jmin<int>(buffer.getNumChannels(), static_cast<int>(maxChannels));
    for (int channel = 0; channel < channels; ++channel)
        juce::FloatVectorOperations::copy(dryBuffer.data() + channel * samplesPerChannel,
                                          buffer.getReadPointer(channel), samplesPerChannel);
    const auto plate = juce::jlimit(0, 3, static_cast<int>(std::lround(value(state, "plate"))));
    // Input stage, phase and the era / tension switches from the faceplate.
    const auto inputGain = gainFromDb(value(state, "input")) * (value(state, "phase") > 0.5f ? -1.0f : 1.0f);
    const auto vintage = value(state, "era") > 0.5f;
    const auto tension = juce::jlimit(0, 2, static_cast<int>(std::lround(value(state, "tension", 1.0f))));
    const auto hpfEngaged = value(state, "hpf", 1.0f) > 0.5f;
    plateBypass.setTargetValue(value(state, "bypass") > 0.5f ? 1.0f : 0.0f);
    for (int channel = 0; channel < channels; ++channel)
        buffer.applyGain(channel, 0, samplesPerChannel, inputGain);
    const auto preDelaySamples = static_cast<size_t>(std::lround(value(state, "pre_delay", 25.0f)
                                                                 * 0.001 * sampleRate));
    const auto decay = value(state, "decay", 2.5f);
    // A slack plate rings longer and darker; a tight one is shorter and brighter.
    // Vintage adds the extra HF loss of an older transducer pair.
    const std::array<float, 3> tensionScale { 1.08f, 1.0f, 0.93f };
    const std::array<float, 3> tensionDamping { 0.06f, 0.0f, -0.04f };
    const auto damping = juce::jlimit(0.02f, 0.98f, value(state, "damp", 45.0f) * 0.009f + 0.05f
                                      + tensionDamping[static_cast<size_t>(tension)] + (vintage ? 0.10f : 0.0f));
    const auto drive = value(state, "drive", 10.0f) * 0.01f;
    const auto crosstalk = value(state, "crosstalk", 25.0f) * 0.01f;
    const auto noiseAmount = value(state, "noise") * 0.000001f;
    const auto width = value(state, "width", 110.0f) * 0.01f;
    const std::array<double, plateLineCount> baseTimes { 0.0297, 0.0371, 0.0411, 0.0437 };
    const auto plateScale = (0.88 + 0.08 * plate) * tensionScale[static_cast<size_t>(tension)];

    for (int channel = 0; channel < channels; ++channel)
    {
        auto& preDelay = platePreDelay[static_cast<size_t>(channel)];
        auto& prePosition = platePreDelayPosition[static_cast<size_t>(channel)];
        auto& random = noiseState[static_cast<size_t>(channel)];
        if (hpfEngaged)
            plateToneFilters[static_cast<size_t>(channel)][0].setHighPass(
                sampleRate, value(state, "bass_cut", 80.0f));
        else
            plateToneFilters[static_cast<size_t>(channel)][0].setIdentity();
        plateToneFilters[static_cast<size_t>(channel)][1].setHighShelf(
            sampleRate, 6500.0f, gainFromDb(value(state, "treble")));
        if (vintage)
            plateToneFilters[static_cast<size_t>(channel)][2].setFirstOrderLowPass(sampleRate, 5200.0f);
        else
            plateToneFilters[static_cast<size_t>(channel)][2].setIdentity();
        auto* output = buffer.getWritePointer(channel);
        for (int sample = 0; sample < samplesPerChannel; ++sample)
        {
            preDelay[prePosition] = output[sample];
            const auto read = (prePosition + preDelay.size()
                               - juce::jmin(preDelaySamples, preDelay.size() - 1)) % preDelay.size();
            const auto input = std::tanh(preDelay[read] * (1.0f + drive * 2.0f));
            prePosition = (prePosition + 1) % preDelay.size();

            std::array<float, plateLineCount> taps {};
            std::array<float, plateLineCount> damped {};
            for (size_t line = 0; line < plateLineCount; ++line)
            {
                auto& storage = plateLines[static_cast<size_t>(channel)][line];
                auto& position = platePositions[static_cast<size_t>(channel)][line];
                const auto length = juce::jlimit<size_t>(2, storage.size() - 1,
                    static_cast<size_t>(std::lround(baseTimes[line] * plateScale * sampleRate)));
                const auto readPosition = (position + storage.size() - length) % storage.size();
                taps[line] = storage[readPosition];
                plateDampingState[static_cast<size_t>(channel)][line]
                    += damping * (taps[line] - plateDampingState[static_cast<size_t>(channel)][line]);
                damped[line] = plateDampingState[static_cast<size_t>(channel)][line];
            }
            // Energy-preserving 4x4 Hadamard feedback matrix. The previous shared-sum
            // feedback could exceed unity and howl with dense material.
            const std::array<float, plateLineCount> scattered {
                (damped[0] + damped[1] + damped[2] + damped[3]) * 0.5f,
                (damped[0] - damped[1] + damped[2] - damped[3]) * 0.5f,
                (damped[0] + damped[1] - damped[2] - damped[3]) * 0.5f,
                (damped[0] - damped[1] - damped[2] + damped[3]) * 0.5f
            };
            for (size_t line = 0; line < plateLineCount; ++line)
            {
                auto& storage = plateLines[static_cast<size_t>(channel)][line];
                auto& position = platePositions[static_cast<size_t>(channel)][line];
                const auto lineSeconds = baseTimes[line] * plateScale;
                const auto feedback = static_cast<float>(std::pow(0.001, lineSeconds / decay));
                storage[position] = input * 0.24f + std::tanh(scattered[line]) * feedback;
                position = (position + 1) % storage.size();
            }
            random = random * 1664525u + 1013904223u;
            auto wet = (taps[0] + taps[1] - taps[2] + taps[3]) * 0.24f;
            wet += (static_cast<float>(random >> 8) / 8388607.5f - 1.0f) * noiseAmount;
            wet = std::tanh(wet * 1.2f) * 0.85f;
            output[sample] = plateToneFilters[static_cast<size_t>(channel)][2].process(
                plateToneFilters[static_cast<size_t>(channel)][1].process(
                    plateToneFilters[static_cast<size_t>(channel)][0].process(wet)));
        }
    }

    if (channels == 2)
        for (int sample = 0; sample < samplesPerChannel; ++sample)
        {
            const auto left = buffer.getSample(0, sample), right = buffer.getSample(1, sample);
            const auto mid = (left + right) * 0.5f;
            const auto side = (left - right) * 0.5f * width;
            buffer.setSample(0, sample, mid + side + right * crosstalk * 0.1f);
            buffer.setSample(1, sample, mid - side + left * crosstalk * 0.1f);
        }
    // Mix against the untouched input, output trim on the processed signal only,
    // then a click-free bypass crossfade back to the untouched input.
    const auto mix = juce::jlimit(0.0f, 1.0f, value(state, "mix", 100.0f) * 0.01f);
    const auto outputGain = gainFromDb(value(state, "output"));
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* wet = buffer.getWritePointer(channel);
        const auto* dry = dryBuffer.data() + channel * samplesPerChannel;
        auto bypass = plateBypass;
        for (int sample = 0; sample < samplesPerChannel; ++sample)
        {
            const auto processed = (dry[sample] * inputGain + mix * (wet[sample] - dry[sample] * inputGain)) * outputGain;
            const auto amount = bypass.getNextValue();
            wet[sample] = processed + amount * (dry[sample] - processed);
        }
        if (channel == channels - 1) plateBypass = bypass;
    }
}

void AnchorDSP::processLimiter(juce::AudioBuffer<float>& buffer,
                               const juce::AudioProcessorValueTreeState& state)
{
    const auto samples = buffer.getNumSamples();
    const auto channels = juce::jmin<int>(buffer.getNumChannels(), static_cast<int>(maxChannels));
    for (int channel = 0; channel < channels; ++channel)
        juce::FloatVectorOperations::copy(dryBuffer.data() + channel * samples,
                                          buffer.getReadPointer(channel), samples);
    const auto targetOffset = value(state, "target_lufs", -9.0f) + 9.0f;
    const auto drive = gainFromDb(-value(state, "threshold", -8.0f) + targetOffset);
    const auto ceiling = gainFromDb(value(state, "ceiling", -1.0f));
    const auto lookahead = static_cast<size_t>(std::lround(value(state, "lookahead", 1.0f) * 0.001 * sampleRate));
    auto releaseMs = value(state, "release", 250.0f);
    if (value(state, "release_mode") < 0.5f) releaseMs *= 0.7f;
    const auto style = static_cast<int>(std::lround(value(state, "style", 1.0f)));
    releaseMs *= std::array<float, 4> { 1.35f, 1.0f, 0.72f, 0.5f }[static_cast<size_t>(juce::jlimit(0, 3, style))];
    const auto releaseCoefficient = std::exp(-1.0f / (0.001f * releaseMs * static_cast<float>(sampleRate)));
    const auto link = value(state, "stereo_link", 100.0f) * 0.01f;
    const auto preserve = value(state, "transient_preserve", 40.0f) * 0.002f;
    const auto truePeak = value(state, "true_peak", 1.0f) > 0.5f;
    const auto oversamplingIndex = juce::jlimit(0, 4, static_cast<int>(std::lround(value(state, "oversampling", 3.0f))));
    const auto detectorSteps = truePeak ? (1 << oversamplingIndex) : 1;
    const auto delta = value(state, "delta") > 0.5f;

    for (int sample = 0; sample < samples; ++sample)
    {
        std::array<float, maxChannels> peaks {};
        float linkedPeak = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto current = buffer.getSample(channel, sample) * drive;
            peaks[static_cast<size_t>(channel)] = std::abs(current);
            if (truePeak)
                for (int step = 1; step < detectorSteps; ++step)
                {
                    const auto fraction = static_cast<float>(step) / static_cast<float>(detectorSteps);
                    const auto interpolated = juce::jmap(fraction, limiterPrevious[static_cast<size_t>(channel)], current);
                    peaks[static_cast<size_t>(channel)] = juce::jmax(peaks[static_cast<size_t>(channel)], std::abs(interpolated));
                }
            limiterPrevious[static_cast<size_t>(channel)] = current;
            linkedPeak = juce::jmax(linkedPeak, peaks[static_cast<size_t>(channel)]);
            auto& delay = limiterDelay[static_cast<size_t>(channel)];
            delay[limiterPosition[static_cast<size_t>(channel)]] = current;
        }
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto detector = juce::jmap(link, peaks[static_cast<size_t>(channel)], linkedPeak);
            auto desired = detector > ceiling ? ceiling / detector : 1.0f;
            desired = juce::jmin(1.0f, desired + preserve * (1.0f - desired));
            auto& gain = limiterGain[static_cast<size_t>(channel)];
            gain = desired < gain ? desired : desired + releaseCoefficient * (gain - desired);
            auto& delay = limiterDelay[static_cast<size_t>(channel)];
            const auto read = (limiterPosition[static_cast<size_t>(channel)] + delay.size()
                               - juce::jmin(lookahead, delay.size() - 1)) % delay.size();
            const auto limited = juce::jlimit(-ceiling, ceiling, delay[read] * gain);
            const auto dry = dryBuffer[static_cast<size_t>(channel * samples + sample)];
            buffer.setSample(channel, sample, delta ? dry - limited : limited);
            limiterPosition[static_cast<size_t>(channel)] = (limiterPosition[static_cast<size_t>(channel)] + 1) % delay.size();
        }
    }
}

void AnchorDSP::processDeesser(juce::AudioBuffer<float>& buffer,
                               const juce::AudioProcessorValueTreeState& state)
{
    const auto samples = buffer.getNumSamples();
    const auto channels = juce::jmin<int>(buffer.getNumChannels(), static_cast<int>(maxChannels));
    const auto profile = static_cast<int>(std::lround(value(state, "voice_profile")));
    const auto focus = juce::jlimit(1500.0f, static_cast<float>(sampleRate * 0.4),
                                    value(state, "focus", 6500.0f)
                                    * std::array<float, 3> { 1.0f, 1.15f, 0.85f }[static_cast<size_t>(juce::jlimit(0, 2, profile))]);
    const auto bandwidth = value(state, "bandwidth", 1.2f);
    const auto q = juce::jlimit(0.3f, 12.0f, 1.0f / juce::jmax(0.1f, bandwidth * 0.55f));
    float linkedEnvelope = 0.0f;
    for (int channel = 0; channel < channels; ++channel)
    {
        deesserDetectors[static_cast<size_t>(channel)].setBandPass(sampleRate, focus, q);
        deesserEnvelopes[static_cast<size_t>(channel)].setAttackRelease(
            value(state, "attack", 2.0f), value(state, "release", 120.0f));
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto detector = deesserDetectors[static_cast<size_t>(channel)].process(buffer.getSample(channel, sample));
            dryBuffer[static_cast<size_t>(channel * samples + sample)] = detector;
            deesserEnvelopes[static_cast<size_t>(channel)].processSample(detector);
        }
        linkedEnvelope = juce::jmax(linkedEnvelope, deesserEnvelopes[static_cast<size_t>(channel)].current());
    }
    const auto levelDb = juce::Decibels::gainToDecibels(linkedEnvelope, -120.0f)
                         + value(state, "sensitivity");
    const auto reductionDb = levelDb > value(state, "threshold", -24.0f)
        ? -juce::jmin(value(state, "range", 8.0f), levelDb - value(state, "threshold", -24.0f)) : 0.0f;
    const auto mode = static_cast<int>(std::lround(value(state, "mode", 1.0f)));
    const auto listen = value(state, "listen") > 0.5f;
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* output = buffer.getWritePointer(channel);
        if (listen)
        {
            juce::FloatVectorOperations::copy(output, dryBuffer.data() + channel * samples, samples);
            continue;
        }
        if (mode == 0)
            juce::FloatVectorOperations::multiply(output, gainFromDb(reductionDb), samples);
        else
        {
            deesserFilters[static_cast<size_t>(channel)].setPeak(sampleRate, focus, q,
                                                                 gainFromDb(reductionDb));
            const auto splitAmount = mode == 2 ? 0.7f : 1.0f;
            for (int sample = 0; sample < samples; ++sample)
            {
                const auto dry = output[sample];
                const auto filtered = deesserFilters[static_cast<size_t>(channel)].process(dry);
                output[sample] = dry + splitAmount * (filtered - dry);
            }
        }
    }
}

void AnchorDSP::processResonance(juce::AudioBuffer<float>& buffer,
                                 const juce::AudioProcessorValueTreeState& state)
{
    const auto samples = buffer.getNumSamples();
    const auto channels = juce::jmin<int>(buffer.getNumChannels(), static_cast<int>(maxChannels));
    for (int channel = 0; channel < channels; ++channel)
        juce::FloatVectorOperations::copy(dryBuffer.data() + channel * samples,
                                          buffer.getReadPointer(channel), samples);
    const auto stereoMode = static_cast<int>(std::lround(value(state, "stereo_mode")));
    const auto midSide = channels == 2 && (stereoMode == 1 || stereoMode == 2);
    if (midSide)
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto left = buffer.getSample(0, sample), right = buffer.getSample(1, sample);
            buffer.setSample(0, sample, (left + right) * 0.5f);
            buffer.setSample(1, sample, (left - right) * 0.5f);
        }
    const auto channelActive = [stereoMode](int channel)
    {
        if (stereoMode == 1 || stereoMode == 3) return channel == 0;
        if (stereoMode == 2 || stereoMode == 4) return channel == 1;
        return true;
    };
    const auto low = value(state, "low_freq", 80.0f);
    const auto high = juce::jlimit(low * 1.1f, static_cast<float>(sampleRate * 0.42),
                                   value(state, "high_freq", 18000.0f));
    const auto selectivity = value(state, "selectivity", 60.0f) * 0.01f;
    const auto q = 1.0f + selectivity * 10.0f;
    const auto attack = 2.0f + (100.0f - value(state, "speed", 50.0f)) * 0.25f;
    const auto release = 40.0f + value(state, "smooth", 35.0f) * 5.0f;
    const auto learning = value(state, "learn") > 0.5f;
    const auto frozen = value(state, "freeze") > 0.5f;
    for (size_t band = 0; band < resonanceBandCount; ++band)
    {
        const auto t = static_cast<float>(band) / static_cast<float>(resonanceBandCount - 1);
        const auto frequency = low * std::pow(high / low, t);
        float linked = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            if (!channelActive(channel)) continue;
            auto& detector = resonanceDetectors[static_cast<size_t>(channel)][band];
            auto& envelope = resonanceEnvelopes[band][static_cast<size_t>(channel)];
            detector.setBandPass(sampleRate, frequency, q);
            envelope.setAttackRelease(attack, release);
            for (int sample = 0; sample < samples; ++sample)
                envelope.processSample(detector.process(buffer.getSample(channel, sample)));
            linked = juce::jmax(linked, envelope.current());
        }
        if (learning || !resonanceProfileValid)
            resonanceProfile[band] += 0.08f * (linked - resonanceProfile[band]);
        const auto detectorLevel = frozen && resonanceProfileValid ? resonanceProfile[band] : linked;
        const auto sensitivity = value(state, "sensitivity", 50.0f) * 0.01f;
        const auto excess = juce::jmax(0.0f, juce::Decibels::gainToDecibels(detectorLevel, -120.0f)
                                             + 42.0f + sensitivity * 18.0f);
        const auto reduction = -juce::jmin(18.0f, excess * value(state, "depth", 35.0f) * 0.012f);
        for (int channel = 0; channel < channels; ++channel)
        {
            if (!channelActive(channel)) continue;
            auto& filter = resonanceFilters[static_cast<size_t>(channel)][band];
            filter.setPeak(sampleRate, frequency, q, gainFromDb(reduction));
            auto* output = buffer.getWritePointer(channel);
            for (int sample = 0; sample < samples; ++sample) output[sample] = filter.process(output[sample]);
        }
    }
    if (learning || !resonanceProfileValid) resonanceProfileValid = true;
    if (midSide)
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto mid = buffer.getSample(0, sample), side = buffer.getSample(1, sample);
            buffer.setSample(0, sample, mid + side);
            buffer.setSample(1, sample, mid - side);
        }
    const auto transientKeep = value(state, "transient_keep", 65.0f) * 0.01f;
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* output = buffer.getWritePointer(channel);
        const auto* dry = dryBuffer.data() + channel * samples;
        analogDynamicsEnvelopes[static_cast<size_t>(channel)].setAttackRelease(12.0f, 100.0f);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto envelope = analogDynamicsEnvelopes[static_cast<size_t>(channel)].processSample(dry[sample]);
            const auto transient = juce::jlimit(0.0f, 1.0f, (std::abs(dry[sample]) - envelope) * 5.0f);
            output[sample] = juce::jmap(transient * transientKeep, output[sample], dry[sample]);
        }
    }
    if (value(state, "delta") > 0.5f)
        for (int channel = 0; channel < channels; ++channel)
        {
            auto* output = buffer.getWritePointer(channel);
            const auto* dry = dryBuffer.data() + channel * samples;
            for (int sample = 0; sample < samples; ++sample) output[sample] = dry[sample] - output[sample];
        }
}

void AnchorDSP::processFrequencyShaper(juce::AudioBuffer<float>& buffer,
                                       const juce::AudioProcessorValueTreeState& state)
{
    const auto samples = buffer.getNumSamples();
    const auto channels = juce::jmin<int>(buffer.getNumChannels(), static_cast<int>(maxChannels));
    const auto scratch = [this](int channel, size_t slot) { return spectraBuffer.data() + (static_cast<size_t>(channel) * (spectraBands + 2) + slot) * spectraStride; };
    for (int c=0;c<channels;++c){juce::FloatVectorOperations::copy(dryBuffer.data()+c*samples,buffer.getReadPointer(c),samples);juce::FloatVectorOperations::copy(scratch(c,spectraBands),buffer.getReadPointer(c),samples);}
    float previous=20.0f;
    for(int x=0;x<5;++x){const auto id="xover."+juce::String(x+1).paddedLeft('0',2)+".frequency";const auto maximum=static_cast<float>(sampleRate*.44);const auto frequency=juce::jlimit(juce::jmin(maximum,previous*1.25f),maximum,value(state,id,1000.0f));previous=frequency;
        for(int c=0;c<channels;++c){const auto sourceSlot=spectraBands+static_cast<size_t>(x&1),destinationSlot=spectraBands+static_cast<size_t>((x+1)&1);const auto*source=scratch(c,sourceSlot);auto*low=scratch(c,static_cast<size_t>(x));auto*high=scratch(c,destinationSlot);auto&lp=spectraLowPass[static_cast<size_t>(c)][static_cast<size_t>(x)];auto&hp=spectraHighPass[static_cast<size_t>(c)][static_cast<size_t>(x)];for(auto&f:lp)f.setLowPass(sampleRate,frequency);for(auto&f:hp)f.setHighPass(sampleRate,frequency);for(int i=0;i<samples;++i){auto l=source[i],h=source[i];for(auto&f:lp)l=f.process(l);for(auto&f:hp)h=f.process(h);low[i]=l;high[i]=h;}}}
    for(int c=0;c<channels;++c)juce::FloatVectorOperations::copy(scratch(c,5),scratch(c,spectraBands+1),samples);
    bool anySolo=false;for(int z=0;z<6;++z)anySolo=anySolo||value(state,"zone."+juce::String(z+1).paddedLeft('0',2)+".solo")>.5f;
    for(int z=0;z<6;++z){const auto prefix="zone."+juce::String(z+1).paddedLeft('0',2);const auto enabled=value(state,prefix+".enabled",1)>.5f;const auto solo=value(state,prefix+".solo")>.5f;const auto amount=value(state,prefix+".harmonics")/48.0f;const auto density=value(state,prefix+".density",50)*.01f;for(int c=0;c<channels;++c){auto*b=scratch(c,static_cast<size_t>(z));if(!enabled||(anySolo&&!solo)){juce::FloatVectorOperations::clear(b,samples);continue;}for(int i=0;i<samples;++i){const auto dry=b[i];const auto wet=std::tanh(dry*(1.0f+amount*(2.0f+density*4.0f)));b[i]=juce::jmap(amount,dry,wet);}}if(channels==2){const auto width=value(state,prefix+".width",100)*.01f;auto*l=scratch(0,static_cast<size_t>(z));auto*r=scratch(1,static_cast<size_t>(z));for(int i=0;i<samples;++i){const auto m=(l[i]+r[i])*.5f,s=(l[i]-r[i])*.5f*width;l[i]=m+s;r[i]=m-s;}}}
    const auto wetMix=value(state,"global_mix",50)*.01f,direct=gainFromDb(value(state,"direct"));
    const auto punch=value(state,"punch")*.01f;
    float harmonicTotal=0.0f;
    for(int z=0;z<6;++z)harmonicTotal+=value(state,"zone."+juce::String(z+1).paddedLeft('0',2)+".harmonics")/48.0f;
    const auto automaticGain=value(state,"auto_gain",1)>.5f?1.0f/(1.0f+harmonicTotal*.05f):1.0f;
    for(int c=0;c<channels;++c)
    {
        auto*out=buffer.getWritePointer(c);const auto*dry=dryBuffer.data()+c*samples;
        analogDynamicsEnvelopes[static_cast<size_t>(c)].setAttackRelease(1.0f,80.0f);
        for(int i=0;i<samples;++i)
        {
            float wet=0;for(int z=0;z<6;++z)wet+=scratch(c,static_cast<size_t>(z))[i];
            const auto envelope=analogDynamicsEnvelopes[static_cast<size_t>(c)].processSample(dry[i]);
            const auto transient=juce::jmax(0.0f,std::abs(dry[i])-envelope);
            wet*=automaticGain*juce::jmax(0.0f,1.0f+punch*transient*4.0f);
            out[i]=dry[i]*direct+wet*wetMix;
        }
    }
}

void AnchorDSP::processOrbit(juce::AudioBuffer<float>& buffer,const juce::AudioProcessorValueTreeState& state)
{
    const auto samples = buffer.getNumSamples();
    const auto channels = juce::jmin<int>(buffer.getNumChannels(), static_cast<int>(maxChannels));
    for (int channel = 0; channel < channels; ++channel)
    {
        juce::FloatVectorOperations::copy(dryBuffer.data() + channel * samples,
                                          buffer.getReadPointer(channel), samples);
        orbitDuckEnvelopes[static_cast<size_t>(channel)].setAttackRelease(5.0f, 120.0f);
        for (int tap = 0; tap < 8; ++tap)
        {
            const auto prefix = "tap." + juce::String(tap + 1).paddedLeft('0', 2);
            orbitTapFilters[static_cast<size_t>(channel)][static_cast<size_t>(tap)].setLowPass(
                sampleRate, juce::jlimit(20.0f, static_cast<float>(sampleRate * 0.45),
                                         value(state, prefix + ".filter", 8000.0f)));
        }
    }

    const auto feedback = juce::jlimit(0.0f, 0.98f, value(state, "feedback", 35.0f) * 0.01f);
    const auto mix = juce::jlimit(0.0f, 1.0f, value(state, "mix", 25.0f) * 0.01f);
    const auto duck = juce::jlimit(0.0f, 1.0f, value(state, "duck", 25.0f) * 0.01f);
    const auto freeze = value(state, "freeze") > 0.5f;
    auto baseTime = value(state, "time", 375.0f);
    if (value(state, "tempo_sync", 1.0f) > 0.5f)
        baseTime = juce::jmax(31.25f, std::round(baseTime / 31.25f) * 31.25f);
    const auto modRate = value(state, "mod_rate", 0.35f);
    const auto modDepth = value(state, "mod_depth", 3.0f);
    const auto dryGain = std::cos(mix * juce::MathConstants<float>::halfPi);
    const auto wetGain = std::sin(mix * juce::MathConstants<float>::halfPi);

    std::array<bool, 8> tapEnabled {};
    std::array<float, 8> tapTimes {};
    std::array<float, 8> tapLevels {};
    std::array<float, 8> tapPanLeft {};
    std::array<float, 8> tapPanRight {};
    std::array<float, 8> tapPhaseSine {};
    std::array<float, 8> tapPhaseCosine {};
    std::array<float, maxChannels> gainSums {};
    for (int tap = 0; tap < 8; ++tap)
    {
        const auto prefix = "tap." + juce::String(tap + 1).paddedLeft('0', 2);
        tapEnabled[static_cast<size_t>(tap)] = value(state, prefix + ".enabled", 1.0f) >= 0.5f;
        tapTimes[static_cast<size_t>(tap)] = value(state, prefix + ".time", 250.0f)
                                                + baseTime * (static_cast<float>(tap) / 8.0f);
        tapLevels[static_cast<size_t>(tap)] = gainFromDb(value(state, prefix + ".level", -6.0f));
        const auto pan = juce::jlimit(-1.0f, 1.0f, value(state, prefix + ".pan") * 0.01f);
        tapPanLeft[static_cast<size_t>(tap)] = std::sqrt((1.0f - pan) * 0.5f);
        tapPanRight[static_cast<size_t>(tap)] = std::sqrt((1.0f + pan) * 0.5f);
        tapPhaseSine[static_cast<size_t>(tap)] = static_cast<float>(std::sin(static_cast<double>(tap)));
        tapPhaseCosine[static_cast<size_t>(tap)] = static_cast<float>(std::cos(static_cast<double>(tap)));
        if (tapEnabled[static_cast<size_t>(tap)])
        {
            gainSums[0] += tapLevels[static_cast<size_t>(tap)] * tapPanLeft[static_cast<size_t>(tap)];
            gainSums[1] += tapLevels[static_cast<size_t>(tap)] * tapPanRight[static_cast<size_t>(tap)];
        }
    }

    const auto phaseStep = juce::MathConstants<double>::twoPi * modRate / sampleRate;
    auto phaseSine = static_cast<float>(std::sin(orbitPhase));
    auto phaseCosine = static_cast<float>(std::cos(orbitPhase));
    const auto stepSine = static_cast<float>(std::sin(phaseStep));
    const auto stepCosine = static_cast<float>(std::cos(phaseStep));

    for (int sample = 0; sample < samples; ++sample)
    {
        for (int channel = 0; channel < channels; ++channel)
        {
            auto& delay = orbitDelay[static_cast<size_t>(channel)];
            float wet = 0.0f;
            for (int tap = 0; tap < 8; ++tap)
            {
                const auto tapIndex = static_cast<size_t>(tap);
                if (!tapEnabled[tapIndex])
                    continue;
                const auto modulation = phaseSine * tapPhaseCosine[tapIndex]
                                      + phaseCosine * tapPhaseSine[tapIndex];
                const auto delayMs = tapTimes[tapIndex] + modDepth * modulation;
                const auto delaySamples = juce::jlimit<size_t>(
                    1, delay.size() - 1,
                    static_cast<size_t>(std::lround(juce::jmax(0.02f, delayMs) * 0.001 * sampleRate)));
                const auto read = (orbitPosition[static_cast<size_t>(channel)] + delay.size()
                                   - delaySamples) % delay.size();
                const auto panGain = channel == 0 ? tapPanLeft[tapIndex] : tapPanRight[tapIndex];
                wet += orbitTapFilters[static_cast<size_t>(channel)][static_cast<size_t>(tap)]
                           .process(delay[read]) * tapLevels[tapIndex] * panGain;
            }

            wet /= juce::jmax(1.0f, gainSums[static_cast<size_t>(channel)]);
            const auto input = buffer.getSample(channel, sample);
            const auto envelope = orbitDuckEnvelopes[static_cast<size_t>(channel)].processSample(input);
            wet *= juce::jlimit(0.0f, 1.0f, 1.0f - duck * envelope * 2.0f);
            const auto stableWet = std::tanh(wet);
            delay[orbitPosition[static_cast<size_t>(channel)]] = (freeze ? 0.0f : input)
                + stableWet * (freeze ? 0.999f : feedback);
            const auto output = dryGain * input + wetGain * stableWet;
            buffer.setSample(channel, sample, std::isfinite(output) ? output : input);
            orbitPosition[static_cast<size_t>(channel)] =
                (orbitPosition[static_cast<size_t>(channel)] + 1) % delay.size();
        }
        const auto nextSine = phaseSine * stepCosine + phaseCosine * stepSine;
        phaseCosine = phaseCosine * stepCosine - phaseSine * stepSine;
        phaseSine = nextSine;
    }
    orbitPhase = std::fmod(orbitPhase + phaseStep * samples, juce::MathConstants<double>::twoPi);
}

void AnchorDSP::processSpaceReverb(juce::AudioBuffer<float>& buffer,
                                   const juce::AudioProcessorValueTreeState& state)
{
    const auto samples=buffer.getNumSamples();
    const auto channels=juce::jmin<int>(buffer.getNumChannels(),static_cast<int>(maxChannels));
    const auto pre=static_cast<size_t>(std::lround(value(state,"pre_delay",25)*.001*sampleRate));
    const auto decay=value(state,"decay",2.4f);
    const auto size=.55f+value(state,"size",70)*.007f;
    const auto damping=juce::jlimit(.02f,.95f,value(state,"high_damp",.6f)*.65f);
    const auto density=value(state,"density",65)*.01f;
    const auto diffusion=value(state,"diffusion",60)*.01f;
    const auto lateMix=juce::jlimit(0.0f,1.0f,(value(state,"early_late")+100.0f)*.005f);
    const auto modulationRate=value(state,"mod_rate",.35f);
    const auto modulationDepth=value(state,"mod_depth",20)*.0008f;
    const auto duck=value(state,"duck",20)*.01f;
    const auto freeze=value(state,"freeze")>.5f;
    const std::array<double,4>times{.031,.037,.043,.053};

    for(int c=0;c<channels;++c)
    {
        juce::FloatVectorOperations::copy(dryBuffer.data()+c*samples,buffer.getReadPointer(c),samples);
        orbitDuckEnvelopes[static_cast<size_t>(c)].setAttackRelease(5.0f,180.0f);
        plateToneFilters[static_cast<size_t>(c)][0].setLowShelf(sampleRate,180.0f,
            juce::jlimit(.35f,2.0f,value(state,"low_damp",.8f)));
        auto&pd=platePreDelay[static_cast<size_t>(c)];
        auto&pp=platePreDelayPosition[static_cast<size_t>(c)];
        auto*out=buffer.getWritePointer(c);
        for(int i=0;i<samples;++i)
        {
            pd[pp]=out[i];
            const auto readPre=(pp+pd.size()-juce::jmin(pre,pd.size()-1))%pd.size();
            const auto early=pd[readPre];
            const auto input=freeze?0.0f:early;
            pp=(pp+1)%pd.size();
            std::array<float,4>taps{};
            std::array<float,4>damped{};
            for(size_t lineIndex=0;lineIndex<4;++lineIndex)
            {
                auto&line=plateLines[static_cast<size_t>(c)][lineIndex];
                auto&position=platePositions[static_cast<size_t>(c)][lineIndex];
                const auto mod=1.0+modulationDepth*std::sin(orbitPhase+static_cast<double>(lineIndex));
                const auto length=juce::jlimit<size_t>(2,line.size()-1,static_cast<size_t>(times[lineIndex]*size*mod*sampleRate));
                const auto read=(position+line.size()-length)%line.size();
                taps[lineIndex]=line[read];
                plateDampingState[static_cast<size_t>(c)][lineIndex]+=damping*(taps[lineIndex]-plateDampingState[static_cast<size_t>(c)][lineIndex]);
                damped[lineIndex]=plateDampingState[static_cast<size_t>(c)][lineIndex];
            }
            const std::array<float,4>scattered{
                (damped[0]+damped[1]+damped[2]+damped[3])*.5f,
                (damped[0]-damped[1]+damped[2]-damped[3])*.5f,
                (damped[0]+damped[1]-damped[2]-damped[3])*.5f,
                (damped[0]-damped[1]-damped[2]+damped[3])*.5f};
            for(size_t lineIndex=0;lineIndex<4;++lineIndex)
            {
                auto&line=plateLines[static_cast<size_t>(c)][lineIndex];
                auto&position=platePositions[static_cast<size_t>(c)][lineIndex];
                const auto feedback=freeze?.9995f:static_cast<float>(std::pow(.001,times[lineIndex]*size/decay));
                const auto diffusionGain=.72f+diffusion*.26f;
                line[position]=input*(.1f+density*.2f)+std::tanh(scattered[lineIndex]*diffusionGain)*feedback;
                position=(position+1)%line.size();
            }
            auto late=std::tanh((taps[0]+taps[1]-taps[2]+taps[3])*.32f)*.85f;
            late=plateToneFilters[static_cast<size_t>(c)][0].process(late);
            const auto envelope=orbitDuckEnvelopes[static_cast<size_t>(c)].processSample(dryBuffer[static_cast<size_t>(c)*samples+i]);
            const auto wet=juce::jmap(lateMix,early*.35f,late)*(1.0f-juce::jlimit(0.0f,.9f,duck*envelope));
            out[i]=wet;
            orbitPhase+=juce::MathConstants<double>::twoPi*modulationRate/(sampleRate*channels);
        }
    }
    orbitPhase=std::fmod(orbitPhase,juce::MathConstants<double>::twoPi);
    const auto width=value(state,"width",120)*.01f;
    if(channels==2)for(int i=0;i<samples;++i){const auto l=buffer.getSample(0,i),r=buffer.getSample(1,i),m=(l+r)*.5f,s=(l-r)*.5f*width;buffer.setSample(0,i,m+s);buffer.setSample(1,i,m-s);}
    const auto mix=value(state,"mix",25)*.01f;
    for(int c=0;c<channels;++c){auto*out=buffer.getWritePointer(c);const auto*dry=dryBuffer.data()+c*samples;for(int i=0;i<samples;++i)out[i]=dry[i]+mix*(out[i]-dry[i]);}
}

void AnchorDSP::processImager(juce::AudioBuffer<float>& buffer,const juce::AudioProcessorValueTreeState& state)
{
    if(buffer.getNumChannels()<2){buffer.applyGain(gainFromDb(value(state,"output_gain")));return;}
    const auto samples=buffer.getNumSamples();
    const auto scratch=[this](int c,size_t slot){return spectraBuffer.data()+(static_cast<size_t>(c)*(spectraBands+2)+slot)*spectraStride;};
    for(int c=0;c<2;++c)juce::FloatVectorOperations::copy(scratch(c,spectraBands),buffer.getReadPointer(c),samples);
    double cross=0.0,leftEnergy=0.0,rightEnergy=0.0;
    for(int i=0;i<samples;++i){const auto l=buffer.getSample(0,i),r=buffer.getSample(1,i);cross+=l*r;leftEnergy+=l*l;rightEnergy+=r*r;}
    const auto correlation=static_cast<float>(cross/std::sqrt(juce::jmax(1.0e-12,leftEnergy*rightEnergy)));
    std::array<float,4>boundaries{};
    float previous=40;
    for(int x=0;x<4;++x){const auto maximum=static_cast<float>(sampleRate*.44);const auto lower=juce::jmin(maximum,previous*1.2f);const auto f=juce::jlimit(lower,maximum,value(state,"xover."+juce::String(x+1).paddedLeft('0',2)+".frequency",1000));boundaries[static_cast<size_t>(x)]=f;previous=f;for(int c=0;c<2;++c){const auto ss=spectraBands+static_cast<size_t>(x&1),ds=spectraBands+static_cast<size_t>((x+1)&1);const auto*src=scratch(c,ss);auto*lo=scratch(c,static_cast<size_t>(x));auto*hi=scratch(c,ds);auto&lp=spectraLowPass[static_cast<size_t>(c)][static_cast<size_t>(x)];auto&hp=spectraHighPass[static_cast<size_t>(c)][static_cast<size_t>(x)];for(auto&q:lp)q.setLowPass(sampleRate,f);for(auto&q:hp)q.setHighPass(sampleRate,f);for(int i=0;i<samples;++i){auto l=src[i],h=src[i];for(auto&q:lp)l=q.process(l);for(auto&q:hp)h=q.process(h);lo[i]=l;hi[i]=h;}}}
    for(int c=0;c<2;++c)juce::FloatVectorOperations::copy(scratch(c,4),scratch(c,spectraBands),samples);
    const auto focus=value(state,"focus",50)*.02f;
    const auto bassMono=value(state,"bass_mono",120);
    const auto safe=value(state,"safe_width",1)>.5f;
    const auto alarm=value(state,"correlation_alarm");
    for(int b=0;b<5;++b){auto*l=scratch(0,static_cast<size_t>(b));auto*r=scratch(1,static_cast<size_t>(b));const auto requested=value(state,"band."+juce::String(b+1).paddedLeft('0',2)+".width",100)*.01f*value(state,"global_width",100)*.01f;auto width=1.0f+(requested-1.0f)*focus;if(b<4&&boundaries[static_cast<size_t>(b)]<=bassMono*1.01f)width=0.0f;if(safe)width=juce::jmin(width,correlation<alarm?1.0f:2.0f);for(int i=0;i<samples;++i){const auto m=(l[i]+r[i])*.5f,s=(l[i]-r[i])*.5f*width;l[i]=m+s;r[i]=m-s;}}
    const auto balance=value(state,"ms_balance")*.01f,angle=value(state,"rotation")*juce::MathConstants<float>::pi/180.0f;auto*left=buffer.getWritePointer(0);auto*right=buffer.getWritePointer(1);for(int i=0;i<samples;++i){float l=0,r=0;for(int b=0;b<5;++b){l+=scratch(0,static_cast<size_t>(b))[i];r+=scratch(1,static_cast<size_t>(b))[i];}auto m=(l+r)*.5f*(1-balance*.5f),s=(l-r)*.5f*(1+balance*.5f);l=m+s;r=m-s;const auto rl=l*std::cos(angle)-r*std::sin(angle),rr=l*std::sin(angle)+r*std::cos(angle);left[i]=value(state,"mono_check")>.5f?(rl+rr)*.5f:rl;right[i]=value(state,"mono_check")>.5f?left[i]:rr;}buffer.applyGain(gainFromDb(value(state,"output_gain")));
}

void AnchorDSP::processGeneric(juce::AudioBuffer<float>& buffer, const juce::AudioProcessorValueTreeState& state)
{
    const auto input = value(state, "input_gain", value(state, "input_trim"));
    const auto output = value(state, "output_gain", value(state, "output_trim"));
    applyInputAndOutput(buffer, input, output);
}

void AnchorDSP::process(juce::AudioBuffer<float>& buffer,
                        const juce::AudioProcessorValueTreeState& state,
                        const juce::String& pluginId)
{
    juce::ScopedNoDenormals guard;
    if (pluginId == "D01") processPrism(buffer, state);
    else if (pluginId == "D02") processFlux(buffer, state);
    else if (pluginId == "D03") processSpectra(buffer, state);
    else if (pluginId == "D04") processLimiter(buffer, state);
    else if (pluginId == "D05") processDeesser(buffer, state);
    else if (pluginId == "D06") processResonance(buffer, state);
    else if (pluginId == "D07") processFrequencyShaper(buffer, state);
    else if (pluginId == "D08") processOrbit(buffer, state);
    else if (pluginId == "D09") processSpaceReverb(buffer, state);
    else if (pluginId == "D10") processImager(buffer, state);
    else if (pluginId == "A01") processHeritage(buffer, state);
    else if (pluginId == "A02") processIron(buffer, state);
    else if (pluginId == "A03") processConsole(buffer, state);
    else if (pluginId == "A04") processTape(buffer, state);
    else if (pluginId == "A05") processValve(buffer, state);
    else if (pluginId == "A06" || pluginId == "A07" || pluginId == "A08")
        processAnalogCompressor(buffer, state, pluginId);
    else if (pluginId == "A09") processPassiveEq(buffer, state);
    else if (pluginId == "A10") processPlate(buffer, state);
    else processGeneric(buffer, state);
}
}
