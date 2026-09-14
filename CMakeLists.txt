cmake_minimum_required(VERSION 3.25)

project(AmanorsacStudioMixingSuite VERSION 0.1.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
include(CTest)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

set(AMANORSAC_PLUGIN_IDS
    D01 D02 D03 D04 D05 D06 D07 D08 D09 D10
    A01 A02 A03 A04 A05 A06 A07 A08 A09 A10
)

# Audio Units are macOS only, and are what Logic loads. Built there, absent
# everywhere else so the Windows build is unchanged.
set(plugin_formats VST3 Standalone)
if(APPLE)
    list(APPEND plugin_formats AU)
endif()

function(amanorsac_add_plugin plugin_id)
    set(spec_path "${CMAKE_CURRENT_SOURCE_DIR}/specs/plugins/${plugin_id}.json")
    if(NOT EXISTS "${spec_path}")
        message(FATAL_ERROR "Missing machine contract: ${spec_path}")
    endif()

    file(READ "${spec_path}" SPEC_JSON)
    string(JSON DISPLAY_NAME GET "${SPEC_JSON}" display_name)
    string(JSON SERIES GET "${SPEC_JSON}" series)
    string(JSON PRIMARY_ROLE GET "${SPEC_JSON}" primary_role)

    set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/${plugin_id}")
    file(MAKE_DIRECTORY "${generated_dir}")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GeneratedPluginSpec.h.in"
        "${generated_dir}/GeneratedPluginSpec.h"
        @ONLY
    )

    set(target "Amanorsac${plugin_id}")
    set(asset_target "Amanorsac${plugin_id}Assets")
    # Approved v2 faceplate raster, its aperture geometry, and the extracted
    # control sprites are embedded per product so the editor needs no dev paths.
    set(faceplate_assets "")
    set(backdrop_assets "")
    file(GLOB faceplate_png "${CMAKE_CURRENT_SOURCE_DIR}/assets/faceplates/${plugin_id}_*_FACEPLATE.png")
    file(GLOB backdrop_png "${CMAKE_CURRENT_SOURCE_DIR}/assets/backdrops/analog/${plugin_id}_*_CLEAN_BACKDROP.png")
    file(GLOB sprite_pngs "${CMAKE_CURRENT_SOURCE_DIR}/assets/faceplates/sprites/${plugin_id}/*.png")
    set(geometry_json "${CMAKE_CURRENT_SOURCE_DIR}/assets/faceplates/${plugin_id}.json")
    set(poses_json "${CMAKE_CURRENT_SOURCE_DIR}/assets/faceplates/sprites/poses.json")
    if(EXISTS "${geometry_json}" AND faceplate_png)
        list(APPEND faceplate_assets ${faceplate_png} ${sprite_pngs} "${geometry_json}" "${poses_json}")
    endif()
    if(backdrop_png)
        list(APPEND backdrop_assets ${backdrop_png})
    endif()

    set(factory_bank "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/presets/factory/${plugin_id}_factory.json")
        set(factory_bank "presets/factory/${plugin_id}_factory.json")
    endif()

    juce_add_binary_data(${asset_target}
        NAMESPACE AmanorsacBinaryData
        SOURCES
            ${factory_bank}
            assets/brand/Amanorsac_Studio_Official_Logo.JPG
            assets/brand/AmanorsacLogo.png
            assets/fonts/EdenMillsBold.otf
            assets/fonts/EdenMillsRegular.otf
            assets/designs/${plugin_id}.png
            ${backdrop_assets}
            ${faceplate_assets}
    )
    juce_add_plugin(${target}
        COMPANY_NAME "Amanorsac Studio"
        BUNDLE_ID "studio.amanorsac.${plugin_id}"
        PLUGIN_MANUFACTURER_CODE Amns
        PLUGIN_CODE ${plugin_id}P
        PRODUCT_NAME "${DISPLAY_NAME}"
        DESCRIPTION "Amanorsac Studio ${SERIES} ${PRIMARY_ROLE}"
        FORMATS ${plugin_formats}
        COPY_PLUGIN_AFTER_BUILD FALSE
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        # macOS notarisation requires the hardened runtime, and the standalone
        # opens an audio input, so it must declare the entitlement and carry a
        # usage string or Apple rejects the submission.
        HARDENED_RUNTIME_ENABLED TRUE
        HARDENED_RUNTIME_OPTIONS "com.apple.security.device.audio-input"
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "${DISPLAY_NAME} needs the audio input to process sound from your interface."
        VST3_CATEGORIES Fx
    )

    target_sources(${target} PRIVATE
        src/common/audio/PluginProcessor.cpp
        src/common/audio/PluginProcessor.h
        src/common/dsp/AnchorDSP.cpp
        src/common/dsp/AnalogFrontEnd.cpp
        src/common/dsp/AnalogFrontEnd.h
        src/common/dsp/AnchorDSP.h
        src/common/licensing/LicenseClient.cpp
        src/common/licensing/LicenseClient.h
        src/common/licensing/LicenseCrypto.cpp
        src/common/licensing/LicenseCrypto.h
        src/common/licensing/LicenseStore.cpp
        src/common/licensing/LicenseStore.h
        src/common/ui/ActivationView.h
        src/common/presets/PresetManager.cpp
        src/common/presets/PresetManager.h
        src/common/state/PluginSpec.cpp
        src/common/state/PluginSpec.h
        src/common/ui/PluginEditor.cpp
        src/common/ui/PluginEditor.h
        src/common/ui/AnalogChassis.h
        src/common/ui/HeritageEditor.cpp
        src/common/ui/HeritageEditor.h
        src/common/ui/AnalogProductEditor.cpp
        src/common/ui/AnalogProductEditor.h
        src/common/ui/AnalogUI.cpp
        src/common/ui/AnalogUI.h
        src/common/ui/IronPreEditor.cpp
        src/common/ui/IronPreEditor.h
        src/common/ui/AnalogPageEditor.cpp
        src/common/ui/AnalogPageEditor.h
        src/common/ui/FaceplateEditor.cpp
        src/common/ui/FaceplateEditor.h
        src/common/ui/PrismEditor.cpp
        src/common/ui/PrismEditor.h
        src/common/ui/ParameterControl.cpp
        src/common/ui/ParameterControl.h
        src/common/ui/SpectrumGraph.cpp
        src/common/ui/SpectrumGraph.h
        src/common/ui/Theme.h
    )

    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${generated_dir}"
    )

    target_compile_definitions(${target} PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0
        # Flip to 1 at release to arm licence enforcement.
        AMANORSAC_LICENSING_ENABLED=0
    )

    target_link_libraries(${target} PRIVATE
        ${asset_target}
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_dsp
        PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    if(plugin_id STREQUAL "D01" OR plugin_id STREQUAL "A01")
        set(test_target "Amanorsac${plugin_id}AnchorTests")
        add_executable(${test_target}
            tests/dsp/AnchorSmokeTests.cpp
            src/common/dsp/AnchorDSP.cpp
            src/common/dsp/AnalogFrontEnd.cpp
            src/common/dsp/AnalogFrontEnd.h
            src/common/dsp/AnchorDSP.h
            src/common/state/PluginSpec.cpp
            src/common/state/PluginSpec.h
        )
        target_include_directories(${test_target} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${generated_dir}"
        )
        target_compile_definitions(${test_target} PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
        )
        target_link_libraries(${test_target} PRIVATE
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
        )
        add_test(NAME "Amanorsac.${plugin_id}.DspState" COMMAND ${test_target})
    endif()

    if(plugin_id STREQUAL "D02" OR plugin_id STREQUAL "D03")
        set(contract_test_target "Amanorsac${plugin_id}IndexedContractTests")
        add_executable(${contract_test_target}
            tests/state/IndexedContractTests.cpp
            src/common/state/PluginSpec.cpp
            src/common/state/PluginSpec.h
        )
        target_include_directories(${contract_test_target} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${generated_dir}"
        )
        target_compile_definitions(${contract_test_target} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(${contract_test_target} PRIVATE
            juce::juce_audio_processors
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
        )
        add_test(NAME "Amanorsac.${plugin_id}.IndexedContract" COMMAND ${contract_test_target})
    endif()

    if(plugin_id STREQUAL "D07" OR plugin_id STREQUAL "D08" OR plugin_id STREQUAL "D10")
        set(collection_test_target "Amanorsac${plugin_id}CollectionTests")
        add_executable(${collection_test_target} tests/state/CollectionContractTests.cpp
            src/common/state/PluginSpec.cpp src/common/state/PluginSpec.h)
        target_include_directories(${collection_test_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${generated_dir}")
        target_compile_definitions(${collection_test_target} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(${collection_test_target} PRIVATE juce::juce_audio_processors
            juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
        add_test(NAME "Amanorsac.${plugin_id}.IndexedCollection" COMMAND ${collection_test_target})
    endif()

    if(plugin_id STREQUAL "D07" OR plugin_id STREQUAL "D08" OR plugin_id STREQUAL "D09" OR plugin_id STREQUAL "D10")
        set(creative_test_target "Amanorsac${plugin_id}CreativeDigitalTests")
        add_executable(${creative_test_target}
            tests/dsp/CreativeDigitalTests.cpp
            src/common/dsp/AnchorDSP.cpp
            src/common/dsp/AnalogFrontEnd.cpp
            src/common/dsp/AnalogFrontEnd.h
            src/common/dsp/AnchorDSP.h
            src/common/dsp/DynamicsPrimitives.h
            src/common/state/PluginSpec.cpp
            src/common/state/PluginSpec.h)
        target_include_directories(${creative_test_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${generated_dir}")
        target_compile_definitions(${creative_test_target} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(${creative_test_target} PRIVATE juce::juce_audio_processors juce::juce_dsp
            juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
        add_test(NAME "Amanorsac.${plugin_id}.CreativeDigital" COMMAND ${creative_test_target})
    endif()

    if(plugin_id STREQUAL "D02")
        set(dsp_test_target "AmanorsacD02DynamicEqTests")
        add_executable(${dsp_test_target}
            tests/dsp/FluxDynamicEqTests.cpp
            src/common/dsp/AnchorDSP.cpp
            src/common/dsp/AnalogFrontEnd.cpp
            src/common/dsp/AnalogFrontEnd.h
            src/common/dsp/AnchorDSP.h
            src/common/dsp/DynamicsPrimitives.h
            src/common/state/PluginSpec.cpp
            src/common/state/PluginSpec.h
        )
        target_include_directories(${dsp_test_target} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${generated_dir}"
        )
        target_compile_definitions(${dsp_test_target} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(${dsp_test_target} PRIVATE
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
        )
        add_test(NAME Amanorsac.D02.DynamicEqCore COMMAND ${dsp_test_target})
    endif()

    if(plugin_id STREQUAL "D03")
        set(dsp_test_target "AmanorsacD03MultibandTests")
        add_executable(${dsp_test_target}
            tests/dsp/SpectraMultibandTests.cpp
            src/common/dsp/AnchorDSP.cpp
            src/common/dsp/AnalogFrontEnd.cpp
            src/common/dsp/AnalogFrontEnd.h
            src/common/dsp/AnchorDSP.h
            src/common/dsp/DynamicsPrimitives.h
            src/common/state/PluginSpec.cpp
            src/common/state/PluginSpec.h
        )
        target_include_directories(${dsp_test_target} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${generated_dir}"
        )
        target_compile_definitions(${dsp_test_target} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(${dsp_test_target} PRIVATE
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
        )
        add_test(NAME Amanorsac.D03.MultibandCore COMMAND ${dsp_test_target})
    endif()

    if(plugin_id MATCHES "^D0[4-6]$")
        set(digital_control_target "Amanorsac${plugin_id}ControlTests")
        add_executable(${digital_control_target} tests/dsp/DigitalControlTests.cpp
            src/common/dsp/AnchorDSP.cpp src/common/dsp/AnchorDSP.h src/common/dsp/DynamicsPrimitives.h
            src/common/state/PluginSpec.cpp src/common/state/PluginSpec.h)
        target_include_directories(${digital_control_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${generated_dir}")
        target_compile_definitions(${digital_control_target} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(${digital_control_target} PRIVATE juce::juce_audio_processors juce::juce_dsp
            juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
        add_test(NAME "Amanorsac.${plugin_id}.DigitalCore" COMMAND ${digital_control_target})
    endif()

    if(plugin_id MATCHES "^A0(2|3|4|5|9)$")
        set(analog_test_target "Amanorsac${plugin_id}CharacterTests")
        add_executable(${analog_test_target}
            tests/dsp/AnalogCharacterTests.cpp
            src/common/dsp/AnchorDSP.cpp
            src/common/dsp/AnalogFrontEnd.cpp
            src/common/dsp/AnalogFrontEnd.h
            src/common/dsp/AnchorDSP.h
            src/common/dsp/DynamicsPrimitives.h
            src/common/state/PluginSpec.cpp
            src/common/state/PluginSpec.h
        )
        target_include_directories(${analog_test_target} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${generated_dir}"
        )
        target_compile_definitions(${analog_test_target} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(${analog_test_target} PRIVATE
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
        )
        add_test(NAME "Amanorsac.${plugin_id}.AnalogCharacter" COMMAND ${analog_test_target})
    endif()

    if(plugin_id STREQUAL "A01")
        add_executable(AmanorsacA01HeritageTests tests/dsp/HeritageEqTests.cpp
            src/common/dsp/AnchorDSP.cpp src/common/dsp/AnchorDSP.h src/common/dsp/DynamicsPrimitives.h
            src/common/state/PluginSpec.cpp src/common/state/PluginSpec.h)
        target_include_directories(AmanorsacA01HeritageTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${generated_dir}")
        target_compile_definitions(AmanorsacA01HeritageTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(AmanorsacA01HeritageTests PRIVATE juce::juce_audio_processors juce::juce_dsp
            juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
        add_test(NAME Amanorsac.A01.HeritageEq COMMAND AmanorsacA01HeritageTests)
    endif()

    if(plugin_id MATCHES "^A0[6-8]$")
        set(compressor_test_target "Amanorsac${plugin_id}CompressorTests")
        add_executable(${compressor_test_target}
            tests/dsp/AnalogCompressorTests.cpp
            src/common/dsp/AnchorDSP.cpp src/common/dsp/AnchorDSP.h
            src/common/dsp/DynamicsPrimitives.h
            src/common/state/PluginSpec.cpp src/common/state/PluginSpec.h)
        target_include_directories(${compressor_test_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${generated_dir}")
        target_compile_definitions(${compressor_test_target} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(${compressor_test_target} PRIVATE juce::juce_audio_processors juce::juce_dsp
            juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
        add_test(NAME "Amanorsac.${plugin_id}.Compressor" COMMAND ${compressor_test_target})
    endif()

    if(plugin_id STREQUAL "A10")
        add_executable(AmanorsacA10PlateTests tests/dsp/PlateReverbTests.cpp
            src/common/dsp/AnchorDSP.cpp src/common/dsp/AnchorDSP.h src/common/dsp/DynamicsPrimitives.h
            src/common/state/PluginSpec.cpp src/common/state/PluginSpec.h)
        target_include_directories(AmanorsacA10PlateTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${generated_dir}")
        target_compile_definitions(AmanorsacA10PlateTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(AmanorsacA10PlateTests PRIVATE juce::juce_audio_processors juce::juce_dsp
            juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
        add_test(NAME Amanorsac.A10.Plate COMMAND AmanorsacA10PlateTests)
    endif()
endfunction()

foreach(plugin_id IN LISTS AMANORSAC_PLUGIN_IDS)
    amanorsac_add_plugin(${plugin_id})
endforeach()

if(BUILD_TESTING)
  foreach(render_id IN ITEMS A01 A02 A03 A04 A05 A06 A07 A08 A09 A10)
    add_executable(Amanorsac${render_id}EditorRender tests/ui/EditorRender.cpp)
    target_include_directories(Amanorsac${render_id}EditorRender PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${juce_SOURCE_DIR}/modules"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/${render_id}")
    target_compile_definitions(Amanorsac${render_id}EditorRender PRIVATE
        JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_REPORT_APP_USAGE=0)
    target_link_libraries(Amanorsac${render_id}EditorRender PRIVATE
        Amanorsac${render_id}
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  endforeach()

  # The rack renders from its own generated spec directory.
  add_executable(AmanorsacR01EditorRender tests/ui/RackRender.cpp)
  target_include_directories(AmanorsacR01EditorRender PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
      "${juce_SOURCE_DIR}/modules"
      "${rack_generated_dir}")
  target_compile_definitions(AmanorsacR01EditorRender PRIVATE
      JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
      JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_REPORT_APP_USAGE=0)
  target_link_libraries(AmanorsacR01EditorRender PRIVATE
      AmanorsacR01
      juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

# The offline key generator that belonged to the retired node-lock scheme is
# gone. Licences are issued by the server now, the private key never leaves it,
# and the build carries exactly one public key (License Integration Standard R2).

set(rack_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/R01")
file(MAKE_DIRECTORY "${rack_generated_dir}")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/specs/plugins/R01.json" SPEC_JSON)
set(plugin_id R01)
set(DISPLAY_NAME "ANALOG MIX RACK")
set(SERIES "Analog")
set(PRIMARY_ROLE "Fixed Analog Processor Chain")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/GeneratedPluginSpec.h.in"
               "${rack_generated_dir}/GeneratedPluginSpec.h" @ONLY)
foreach(module_suffix IN ITEMS 01 02 03 04 05 06 07 08 09 10)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/specs/plugins/A${module_suffix}.json" A${module_suffix}_JSON)
endforeach()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/RackModuleSpecs.h.in"
               "${rack_generated_dir}/RackModuleSpecs.h" @ONLY)

juce_add_plugin(AmanorsacR01
    COMPANY_NAME "Amanorsac Studio" BUNDLE_ID "studio.amanorsac.R01"
    PLUGIN_MANUFACTURER_CODE Amns PLUGIN_CODE R01P PRODUCT_NAME "ANALOG MIX RACK"
    DESCRIPTION "Amanorsac Studio analog-only processor rack" FORMATS VST3 Standalone
    COPY_PLUGIN_AFTER_BUILD FALSE IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS TRUE VST3_CATEGORIES Fx)
# The rack shows each module's real faceplate, so it carries their backdrops.
file(GLOB rack_backdrops "${CMAKE_CURRENT_SOURCE_DIR}/assets/backdrops/analog/A*_CLEAN_BACKDROP.png")
file(GLOB rack_factory_banks "${CMAKE_CURRENT_SOURCE_DIR}/presets/factory/A*_factory.json")
juce_add_binary_data(AmanorsacR01Assets
    NAMESPACE AmanorsacBinaryData
    SOURCES assets/designs/R01.png
            assets/brand/AmanorsacLogo.png
            assets/fonts/EdenMillsBold.otf
            assets/fonts/EdenMillsRegular.otf
            ${rack_backdrops}
            ${rack_factory_banks})
target_sources(AmanorsacR01 PRIVATE
    src/rack/RackProcessor.cpp src/rack/RackProcessor.h src/rack/RackEditor.cpp src/rack/RackEditor.h
    src/common/dsp/AnchorDSP.cpp src/common/dsp/AnchorDSP.h src/common/dsp/DynamicsPrimitives.h
    src/common/dsp/AnalogFrontEnd.cpp src/common/dsp/AnalogFrontEnd.h
    src/common/presets/PresetManager.cpp src/common/presets/PresetManager.h
    src/common/ui/AnalogUI.cpp src/common/ui/AnalogUI.h src/common/ui/AnalogChassis.h
    src/common/ui/ProductHost.h
    src/common/ui/HeritageEditor.cpp src/common/ui/HeritageEditor.h
    src/common/ui/AnalogPageEditor.cpp src/common/ui/AnalogPageEditor.h
    src/common/state/PluginSpec.cpp src/common/state/PluginSpec.h src/common/ui/Theme.h)
target_include_directories(AmanorsacR01 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${rack_generated_dir}")
target_compile_definitions(AmanorsacR01 PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(AmanorsacR01 PRIVATE AmanorsacR01Assets juce::juce_audio_utils juce::juce_cryptography juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

add_executable(AmanorsacR01RackTests tests/rack/RackTests.cpp
    src/rack/RackProcessor.cpp src/rack/RackProcessor.h src/rack/RackEditor.cpp src/rack/RackEditor.h
    src/common/dsp/AnchorDSP.cpp src/common/dsp/AnchorDSP.h src/common/dsp/DynamicsPrimitives.h
    src/common/dsp/AnalogFrontEnd.cpp src/common/dsp/AnalogFrontEnd.h
    src/common/presets/PresetManager.cpp src/common/presets/PresetManager.h
    src/common/ui/AnalogUI.cpp src/common/ui/AnalogUI.h src/common/ui/AnalogChassis.h
    src/common/ui/ProductHost.h
    src/common/ui/HeritageEditor.cpp src/common/ui/HeritageEditor.h
    src/common/ui/AnalogPageEditor.cpp src/common/ui/AnalogPageEditor.h
    src/common/state/PluginSpec.cpp src/common/state/PluginSpec.h src/common/ui/Theme.h)
target_include_directories(AmanorsacR01RackTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${rack_generated_dir}")
target_compile_definitions(AmanorsacR01RackTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(AmanorsacR01RackTests PRIVATE AmanorsacR01Assets juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME Amanorsac.R01.Rack COMMAND AmanorsacR01RackTests)

# One audit across the whole analog line and the rack that hosts it.
add_executable(AmanorsacAnalogEngineAudit tests/dsp/AnalogEngineAudit.cpp
    src/rack/RackProcessor.cpp src/rack/RackEditor.cpp
    src/common/dsp/AnchorDSP.cpp src/common/dsp/AnalogFrontEnd.cpp
    src/common/presets/PresetManager.cpp src/common/ui/AnalogUI.cpp
    src/common/state/PluginSpec.cpp
    src/common/ui/HeritageEditor.cpp src/common/ui/AnalogPageEditor.cpp
    src/common/licensing/LicenseClient.cpp src/common/licensing/LicenseCrypto.cpp
    src/common/licensing/LicenseStore.cpp)
target_include_directories(AmanorsacAnalogEngineAudit PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src" "${rack_generated_dir}")
target_compile_definitions(AmanorsacAnalogEngineAudit PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(AmanorsacAnalogEngineAudit PRIVATE AmanorsacR01Assets
    juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME Amanorsac.Analog.EngineAudit COMMAND AmanorsacAnalogEngineAudit)

add_executable(AmanorsacDspPrimitivesTests
    tests/dsp/DynamicsPrimitivesTests.cpp
    src/common/dsp/DynamicsPrimitives.h
    src/common/dsp/ProcessorModule.h
)
target_include_directories(AmanorsacDspPrimitivesTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(AmanorsacDspPrimitivesTests PRIVATE
    juce::juce_audio_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
add_test(NAME Amanorsac.DspPrimitives COMMAND AmanorsacDspPrimitivesTests)

if(BUILD_TESTING)
    add_test(
        NAME Amanorsac.ContractValidation
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_contracts.cmake
    )
endif()

# ---------------------------------------------------------------- PERFORM LIVE
# A live channel strip for the Perform Live app: zero latency, no oversampling.
juce_add_plugin(AmanorsacPerformLive
    COMPANY_NAME "Amanorsac Studio" BUNDLE_ID "studio.amanorsac.performlive"
    PLUGIN_MANUFACTURER_CODE Amns PLUGIN_CODE Pfl1 PRODUCT_NAME "PERFORM LIVE"
    DESCRIPTION "Amanorsac Studio live channel strip" FORMATS ${plugin_formats}
    COPY_PLUGIN_AFTER_BUILD FALSE IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS TRUE VST3_CATEGORIES Fx
    MICROPHONE_PERMISSION_ENABLED TRUE MICROPHONE_PERMISSION_TEXT "PERFORM LIVE processes your microphone.")
target_sources(AmanorsacPerformLive PRIVATE
    src/perform/PerformProcessor.cpp src/perform/PerformProcessor.h
    src/perform/PerformEditor.cpp src/perform/PerformEditor.h)
target_include_directories(AmanorsacPerformLive PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(AmanorsacPerformLive PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(AmanorsacPerformLive PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

add_executable(AmanorsacPerformLiveCheck tests/perform/PerformLiveCheck.cpp)
target_include_directories(AmanorsacPerformLiveCheck PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(AmanorsacPerformLiveCheck PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(AmanorsacPerformLiveCheck PRIVATE AmanorsacPerformLive juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags)
add_test(NAME Amanorsac.PerformLive COMMAND AmanorsacPerformLiveCheck)
