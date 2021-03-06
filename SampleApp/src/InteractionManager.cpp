/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *     http://aws.amazon.com/apache2.0/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include <curl/curl.h>
#include <chrono>
#include <iostream>
#include <fstream>
#include <AVSCommon/Utils/Logger/Logger.h>

#include "RegistrationManager/CustomerDataManager.h"
#include "SampleApp/InteractionManager.h"
#include "SampleApp/ConsolePrinter.h"

#ifdef ENABLE_PCC
#include <SampleApp/PhoneCaller.h>
#endif

#ifdef ENABLE_MCC
#include <SampleApp/CalendarClient.h>
#include <SampleApp/MeetingClient.h>
#endif

namespace alexaClientSDK {
namespace sampleApp {

/// String to identify log entries originating from this file.
static const std::string TAG("InteractionManager");

/**
 * Create a LogEntry using this file's TAG and the specified event string.
 *
 * @param The event string for this @c LogEntry.
 */
#define LX(event) alexaClientSDK::avsCommon::utils::logger::LogEntry(TAG, event)

using namespace avsCommon::avs;
using namespace avsCommon::sdkInterfaces;

/// This is a 16 bit 16 kHz little endian linear PCM audio file of "Skill" to be recognized.
std::string SKILL_AUDIO_FILE = "./inputs/Skill_test.wav";
//buffer writer for auido file
std::unique_ptr<alexaClientSDK::avsCommon::avs::AudioInputStream::Writer> m_AudioBufferWriter;
std::string TOKEN_TEXT_FILE = "./inputs/PushbulletToken.txt";
/// This is a 16 bit 16 kHz little endian linear PCM audio file of "Skill" to be recognized.
std::string FLICDOUBLE_AUDIO_FILE = "./inputs/Flic_double.wav";
/// This is a 16 bit 16 kHz little endian linear PCM audio file of "Skill" to be recognized.
std::string FLICHOLD_AUDIO_FILE = "./inputs/Flic_hold.wav";
/// This is a 16 bit 16 kHz little endian linear PCM audio file of "Skill" to be recognized.
std::string FLICCLICK_AUDIO_FILE = "./inputs/Flic_single.wav";
/// This is a 16 bit 16 kHz little endian linear PCM audio file of "Skill" to be recognized.
std::string INTRO_AUDIO_FILE = "./inputs/sentai_intro.wav";


InteractionManager::InteractionManager(
    std::shared_ptr<defaultClient::DefaultClient> client,
    std::shared_ptr<applicationUtilities::resources::audio::MicrophoneInterface> micWrapper,
    std::shared_ptr<sampleApp::UIManager> userInterface,
#ifdef ENABLE_PCC
    std::shared_ptr<sampleApp::PhoneCaller> phoneCaller,
#endif
#ifdef ENABLE_MCC
    std::shared_ptr<sampleApp::MeetingClient> meetingClient,
    std::shared_ptr<sampleApp::CalendarClient> calendarClient,
#endif
    capabilityAgents::aip::AudioProvider holdToTalkAudioProvider,
    capabilityAgents::aip::AudioProvider tapToTalkAudioProvider,
    std::shared_ptr<sampleApp::GuiRenderer> guiRenderer,
    capabilityAgents::aip::AudioProvider wakeWordAudioProvider,

#ifdef POWER_CONTROLLER
    std::shared_ptr<PowerControllerHandler> powerControllerHandler,
#endif
#ifdef TOGGLE_CONTROLLER
    std::shared_ptr<ToggleControllerHandler> toggleControllerHandler,
#endif
#ifdef RANGE_CONTROLLER
    std::shared_ptr<RangeControllerHandler> rangeControllerHandler,
#endif
#ifdef MODE_CONTROLLER
    std::shared_ptr<ModeControllerHandler> modeControllerHandler,
#endif
    std::shared_ptr<avsCommon::sdkInterfaces::CallManagerInterface> callManager,
    std::shared_ptr<avsCommon::sdkInterfaces::diagnostics::DiagnosticsInterface> diagnostics) :
        RequiresShutdown{"InteractionManager"},
        m_client{client},
        m_micWrapper{micWrapper},
        m_userInterface{userInterface},
        m_guiRenderer{guiRenderer},
        m_callManager{callManager},
#ifdef ENABLE_PCC
        m_phoneCaller{phoneCaller},
#endif
#ifdef ENABLE_MCC
        m_meetingClient{meetingClient},
        m_calendarClient{calendarClient},
#endif
        m_holdToTalkAudioProvider{holdToTalkAudioProvider},
        m_tapToTalkAudioProvider{tapToTalkAudioProvider},
        m_wakeWordAudioProvider{wakeWordAudioProvider},
#ifdef POWER_CONTROLLER
        m_powerControllerHandler{powerControllerHandler},
#endif
#ifdef TOGGLE_CONTROLLER
        m_toggleControllerHandler{toggleControllerHandler},
#endif
#ifdef RANGE_CONTROLLER
        m_rangeControllerHandler{rangeControllerHandler},
#endif
#ifdef MODE_CONTROLLER
        m_modeControllerHandler{modeControllerHandler},
#endif
        m_isHoldOccurring{false},
        m_isTapOccurring{false},
        m_isCallConnected{false},
        m_isMicOn{true},
        m_diagnostics{diagnostics} {
    if (m_wakeWordAudioProvider) {
        m_micWrapper->startStreamingMicrophoneData();
    }
};

void InteractionManager::begin() {
    m_executor.submit([this]() {
        m_userInterface->printWelcomeScreen();
        if (m_diagnostics && m_diagnostics->getAudioInjector()) {
            m_userInterface->printAudioInjectionHeader();
        }
        m_userInterface->printHelpScreen();
    });
}

void InteractionManager::help() {
    m_executor.submit([this]() { m_userInterface->printHelpScreen(); 
    
    if (m_AudioBufferWriter == nullptr) {
        m_AudioBufferWriter = m_tapToTalkAudioProvider.stream->createWriter(avsCommon::avs::AudioInputStream::Writer::Policy::NONBLOCKABLE, true);
    }
    if (m_client->notifyOfTapToTalk(m_tapToTalkAudioProvider).get()) {
        sendAudioFileAsRecognize(INTRO_AUDIO_FILE);
        m_isTapOccurring = true;
    }
    });
}

void InteractionManager::limitedHelp() {
    m_executor.submit([this]() { m_userInterface->printLimitedHelp(); });
}

void InteractionManager::settings() {
    m_executor.submit([this]() { m_userInterface->printSettingsScreen(); });
}

#if ENABLE_ENDPOINT_CONTROLLERS_MENU
void InteractionManager::endpointController() {
    m_executor.submit([this]() { m_userInterface->printEndpointControllerScreen(); });
}
#endif

#ifdef POWER_CONTROLLER
void InteractionManager::powerController() {
    m_executor.submit([this]() { m_userInterface->printPowerControllerScreen(); });
}
#endif

#ifdef TOGGLE_CONTROLLER
void InteractionManager::toggleController() {
    m_executor.submit([this]() { m_userInterface->printToggleControllerScreen(); });
}
#endif

#ifdef MODE_CONTROLLER
void InteractionManager::modeController() {
    m_executor.submit([this]() { m_userInterface->printModeControllerScreen(); });
}
#endif

#ifdef RANGE_CONTROLLER
void InteractionManager::rangeController() {
    m_executor.submit([this]() { m_userInterface->printRangeControllerScreen(); });
}
#endif

void InteractionManager::locale() {
    m_executor.submit([this]() { m_userInterface->printLocaleScreen(); });
}

void InteractionManager::alarmVolumeRamp() {
    m_executor.submit([this]() { m_userInterface->printAlarmVolumeRampScreen(); });
}

void InteractionManager::wakewordConfirmation() {
    m_executor.submit([this]() { m_userInterface->printWakeWordConfirmationScreen(); });
}

void InteractionManager::speechConfirmation() {
    m_executor.submit([this]() { m_userInterface->printSpeechConfirmationScreen(); });
}

void InteractionManager::timeZone() {
    m_executor.submit([this]() { m_userInterface->printTimeZoneScreen(); });
}

void InteractionManager::networkInfo() {
    m_executor.submit([this]() { m_userInterface->printNetworkInfoScreen(); });
}

void InteractionManager::networkInfoConnectionTypePrompt() {
    m_executor.submit([this]() { m_userInterface->printNetworkInfoConnectionTypePrompt(); });
}

void InteractionManager::networkInfoESSIDPrompt() {
    m_executor.submit([this]() { m_userInterface->printNetworkInfoESSIDPrompt(); });
}

void InteractionManager::networkInfoBSSIDPrompt() {
    m_executor.submit([this]() { m_userInterface->printNetworkInfoBSSIDPrompt(); });
}

void InteractionManager::networkInfoIpPrompt() {
    m_executor.submit([this]() { m_userInterface->printNetworkInfoIpPrompt(); });
}

void InteractionManager::networkInfoSubnetPrompt() {
    m_executor.submit([this]() { m_userInterface->printNetworkInfoSubnetPrompt(); });
}

void InteractionManager::networkInfoMacPrompt() {
    m_executor.submit([this]() { m_userInterface->printNetworkInfoMacPrompt(); });
}

void InteractionManager::networkInfoDHCPPrompt() {
    m_executor.submit([this]() { m_userInterface->printNetworkInfoDHCPPrompt(); });
}

void InteractionManager::networkInfoStaticIpPrompt() {
    m_executor.submit([this]() { m_userInterface->printNetworkInfoStaticIpPrompt(); });
}

void InteractionManager::doNotDisturb() {
    m_executor.submit([this]() { m_userInterface->printDoNotDisturbScreen(); });
}

void InteractionManager::errorValue() {
    m_executor.submit([this]() { m_userInterface->printErrorScreen(); });
}

void InteractionManager::microphoneToggle() {
    m_executor.submit([this]() {
        if (!m_wakeWordAudioProvider) {
            return;
        }
        if (m_isMicOn) {
            m_isMicOn = false;
            if (m_micWrapper->isStreaming()) {
                m_micWrapper->stopStreamingMicrophoneData();
            }
            m_userInterface->microphoneOff();
        } else {
            m_isMicOn = true;
            if (!m_micWrapper->isStreaming() && m_wakeWordAudioProvider) {
                m_micWrapper->startStreamingMicrophoneData();
            }
            m_userInterface->microphoneOn();
        }
    });
}

void InteractionManager::holdToggled() {
    m_executor.submit([this]() {
        if (!m_isMicOn) {
            return;
        }

        if (!m_isHoldOccurring) {
            if (m_client->notifyOfHoldToTalkStart(m_holdToTalkAudioProvider).get()) {
                m_isHoldOccurring = true;
            }
        } else {
            m_isHoldOccurring = false;
            m_client->notifyOfHoldToTalkEnd();
        }
    });
}

void InteractionManager::tap() {
    m_executor.submit([this]() {
        if (!m_isMicOn) {
            return;
        }

        if (!m_isTapOccurring) {
            if (m_AudioBufferWriter == nullptr) {
                m_AudioBufferWriter = m_tapToTalkAudioProvider.stream->createWriter(avsCommon::avs::AudioInputStream::Writer::Policy::NONBLOCKABLE,true);
            }
            if (m_client->notifyOfTapToTalk(m_tapToTalkAudioProvider).get()) {
                sendAudioFileAsRecognize(SKILL_AUDIO_FILE);
                m_isTapOccurring = true;
            }
        } else {
            m_isTapOccurring = false;
            m_client->notifyOfTapToTalkEnd();
        }
    });
}

static std::size_t writeFunction(void* ptr, size_t size, size_t nmemb, std::string* data) {
    data->append((char*)ptr, size * nmemb);
    return size * nmemb;
}

void InteractionManager::flic() {
    m_executor.submit([this]() {
        if (!m_isMicOn) {
            return;
        }

        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        
        std::string response_string;
        std::string response_audio = "";
        std::string pushbulletToken = "";
        std::string headerToken = "Access-Token: ";
        struct curl_slist* headers = NULL;

        pushbulletToken = readTokenFromFile();

        if (pushbulletToken != "")
        {
            headerToken = headerToken.append(pushbulletToken);

            auto initcurl = curl_easy_init();
            if (initcurl) {
                curl_easy_setopt(initcurl, CURLOPT_URL, "https://api.pushbullet.com/v2/pushes");
                headers = curl_slist_append(headers, headerToken.c_str());
                curl_easy_setopt(initcurl, CURLOPT_HTTPHEADER, headers);
                curl_easy_setopt(initcurl, CURLOPT_USERAGENT, "curl/7.42.0");
                curl_easy_setopt(initcurl, CURLOPT_CUSTOMREQUEST, "DELETE");
                curl_easy_perform(initcurl);
                curl_easy_cleanup(initcurl);
            }
            initcurl = NULL;

            while (true) {

                auto curl = curl_easy_init();

                if (curl) {
                    curl_easy_setopt(curl, CURLOPT_URL, "https://api.pushbullet.com/v2/pushes?active=true&modified_after=1400000000");
                    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
                    headers = curl_slist_append(headers, headerToken.c_str());
                    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                    curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/7.42.0");
                    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50L);
                    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
                    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunction);
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);


                    curl_easy_perform(curl);
                    curl_easy_cleanup(curl);

                    response_audio = "";

                    if (response_string.find(" click") != std::string::npos) {
                        response_audio = FLICCLICK_AUDIO_FILE;
                    }
                    else if (response_string.find("double_click") != std::string::npos) {
                        response_audio = FLICDOUBLE_AUDIO_FILE;
                    }
                    else if (response_string.find("hold") != std::string::npos) {
                        response_audio = FLICHOLD_AUDIO_FILE;
                    }

                    if (response_audio.length() > 0)
                    {

                        auto delcurl = curl_easy_init(); 
                        if (delcurl) {
                            curl_easy_setopt(delcurl, CURLOPT_URL, "https://api.pushbullet.com/v2/pushes");
                            headers = curl_slist_append(headers, headerToken.c_str());
                            curl_easy_setopt(delcurl, CURLOPT_HTTPHEADER, headers);
                            curl_easy_setopt(delcurl, CURLOPT_USERAGENT, "curl/7.42.0");
                            curl_easy_setopt(delcurl, CURLOPT_CUSTOMREQUEST, "DELETE");
                            curl_easy_perform(delcurl);
                            curl_easy_cleanup(delcurl);
                        }
                        initcurl = NULL;

                        if (!m_isTapOccurring) {
                            if (m_AudioBufferWriter == nullptr) {
                                m_AudioBufferWriter = m_tapToTalkAudioProvider.stream->createWriter(avsCommon::avs::AudioInputStream::Writer::Policy::NONBLOCKABLE, true);
                            }
                            if (m_client->notifyOfTapToTalk(m_tapToTalkAudioProvider).get()) {
                                sendAudioFileAsRecognize(response_audio);
                                m_isTapOccurring = true;
                            }
                        }
                        else {
                            m_isTapOccurring = false;
                            m_client->notifyOfTapToTalkEnd();
                        }
                        break;
                    }
                }
                ConsolePrinter::prettyPrint("+----------Waiting for a click--------------+");
                curl = NULL;
                if (std::chrono::steady_clock::now() - start > std::chrono::seconds(30))
                {
                    ConsolePrinter::prettyPrint("+----------No click in 30 seconds--------------+");
                    break;
                }
            }
        }
    });
}

std::string InteractionManager::readTokenFromFile()
{
    std::string Token = "";
    std::string line = "";


    std::ifstream myfile(TOKEN_TEXT_FILE, std::ios_base::out);
    if (myfile.is_open())
    {
        while (getline(myfile, line))
        {
            line.erase(line.find_last_not_of(" \n\r\t") + 1);

            Token += line;
        }
        myfile.close();
    }

    Token.erase(Token.find_last_not_of(" \n\r\t") + 1);

    return Token;

}



std::vector<int16_t> readAudioFromFile(const std::string& fileName, bool* errorOccurred) {
    const int RIFF_HEADER_SIZE = 44;

    std::ifstream inputFile(fileName.c_str(), std::ifstream::binary);
    if (!inputFile.good()) {
        std::cout << "Couldn't open audio file!" << std::endl;
        if (errorOccurred) {
            *errorOccurred = true;
        }
        return {};
    }
    inputFile.seekg(0, std::ios::end);
    int fileLengthInBytes = inputFile.tellg();
    if (fileLengthInBytes <= RIFF_HEADER_SIZE) {
        std::cout << "File should be larger than 44 bytes, which is the size of the RIFF header" << std::endl;
        if (errorOccurred) {
            *errorOccurred = true;
        }
        return {};
    }

    inputFile.seekg(RIFF_HEADER_SIZE, std::ios::beg);

    int numSamples = (fileLengthInBytes - RIFF_HEADER_SIZE) / 2;

    std::vector<int16_t> retVal(numSamples, 0);

    inputFile.read((char*)&retVal[0], numSamples * 2);

    if (inputFile.gcount() != numSamples * 2) {
        std::cout << "Error reading audio file" << std::endl;
        if (errorOccurred) {
            *errorOccurred = true;
        }
        return {};
    }

    inputFile.close();
    if (errorOccurred) {
        *errorOccurred = false;
    }
    return retVal;
}

void InteractionManager::sendAudioFileAsRecognize(std::string audioFile) {
    // Put audio onto the SDS saying "Tell me a joke".
    bool error = false;
    std::string file = audioFile;
    std::vector<int16_t> audioData = readAudioFromFile(file, &error);
    m_AudioBufferWriter->write(audioData.data(), audioData.size());
}

void InteractionManager::stopForegroundActivity() {
    m_executor.submit([this]() { m_client->stopForegroundActivity(); });
}

void InteractionManager::playbackPlay() {
    m_executor.submit([this]() { m_client->getPlaybackRouter()->buttonPressed(PlaybackButton::PLAY); });
}

void InteractionManager::playbackPause() {
    m_executor.submit([this]() { m_client->getPlaybackRouter()->buttonPressed(PlaybackButton::PAUSE); });
}

void InteractionManager::playbackNext() {
    m_executor.submit([this]() { m_client->getPlaybackRouter()->buttonPressed(PlaybackButton::NEXT); });
}

void InteractionManager::playbackPrevious() {
    m_executor.submit([this]() { m_client->getPlaybackRouter()->buttonPressed(PlaybackButton::PREVIOUS); });
}

void InteractionManager::playbackSkipForward() {
    m_executor.submit([this]() { m_client->getPlaybackRouter()->buttonPressed(PlaybackButton::SKIP_FORWARD); });
}

void InteractionManager::playbackSkipBackward() {
    m_executor.submit([this]() { m_client->getPlaybackRouter()->buttonPressed(PlaybackButton::SKIP_BACKWARD); });
}

void InteractionManager::playbackShuffle() {
    sendGuiToggleEvent(GuiRenderer::TOGGLE_NAME_SHUFFLE, PlaybackToggle::SHUFFLE);
}

void InteractionManager::playbackLoop() {
    sendGuiToggleEvent(GuiRenderer::TOGGLE_NAME_LOOP, PlaybackToggle::LOOP);
}

void InteractionManager::playbackRepeat() {
    sendGuiToggleEvent(GuiRenderer::TOGGLE_NAME_REPEAT, PlaybackToggle::REPEAT);
}

void InteractionManager::playbackThumbsUp() {
    sendGuiToggleEvent(GuiRenderer::TOGGLE_NAME_THUMBSUP, PlaybackToggle::THUMBS_UP);
}

void InteractionManager::playbackThumbsDown() {
    sendGuiToggleEvent(GuiRenderer::TOGGLE_NAME_THUMBSDOWN, PlaybackToggle::THUMBS_DOWN);
}

void InteractionManager::sendGuiToggleEvent(const std::string& toggleName, PlaybackToggle toggleType) {
    bool action = false;
    if (m_guiRenderer) {
        action = !m_guiRenderer->getGuiToggleState(toggleName);
    }
    m_executor.submit(
        [this, toggleType, action]() { m_client->getPlaybackRouter()->togglePressed(toggleType, action); });
}

void InteractionManager::speakerControl() {
    m_executor.submit([this]() { m_userInterface->printSpeakerControlScreen(); });
}

void InteractionManager::firmwareVersionControl() {
    m_executor.submit([this]() { m_userInterface->printFirmwareVersionControlScreen(); });
}

void InteractionManager::setFirmwareVersion(avsCommon::sdkInterfaces::softwareInfo::FirmwareVersion firmwareVersion) {
    m_executor.submit([this, firmwareVersion]() { m_client->setFirmwareVersion(firmwareVersion); });
}

void InteractionManager::volumeControl() {
    m_executor.submit([this]() { m_userInterface->printVolumeControlScreen(); });
}

void InteractionManager::adjustVolume(avsCommon::sdkInterfaces::ChannelVolumeInterface::Type type, int8_t delta) {
    m_executor.submit([this, type, delta]() {
        /*
         * Group the unmute action as part of the same affordance that caused the volume change, so we don't
         * send another event. This isn't a requirement by AVS.
         */
        std::future<bool> unmuteFuture = m_client->getSpeakerManager()->setMute(
            type,
            false,
            SpeakerManagerInterface::NotificationProperties(
                SpeakerManagerObserverInterface::Source::LOCAL_API, false, false));
        if (!unmuteFuture.valid()) {
            return;
        }
        unmuteFuture.get();

        std::future<bool> future =
            m_client->getSpeakerManager()->adjustVolume(type, delta, SpeakerManagerInterface::NotificationProperties());
        if (!future.valid()) {
            return;
        }
        future.get();
    });
}

void InteractionManager::setMute(avsCommon::sdkInterfaces::ChannelVolumeInterface::Type type, bool mute) {
    m_executor.submit([this, type, mute]() {
        std::future<bool> future =
            m_client->getSpeakerManager()->setMute(type, mute, SpeakerManagerInterface::NotificationProperties());
        future.get();
    });
}

void InteractionManager::confirmResetDevice() {
    m_executor.submit([this]() { m_userInterface->printResetConfirmation(); });
}

void InteractionManager::resetDevice() {
    // This is a blocking operation. No interaction will be allowed during / after resetDevice
    auto result = m_executor.submit([this]() {
        m_client->getRegistrationManager()->logout();
        m_userInterface->printResetWarning();
    });
    result.wait();
}

void InteractionManager::confirmReauthorizeDevice() {
    m_executor.submit([this]() { m_userInterface->printReauthorizeConfirmation(); });
}

#ifdef ENABLE_COMMS
void InteractionManager::commsControl() {
    m_executor.submit([this]() {
        if (m_client->isCommsEnabled()) {
            m_userInterface->printCommsControlScreen();
        } else {
            m_userInterface->printCommsNotSupported();
        }
    });
}

void InteractionManager::acceptCall() {
    m_executor.submit([this]() {
        if (m_client->isCommsEnabled()) {
            m_client->acceptCommsCall();
        } else {
            m_userInterface->printCommsNotSupported();
        }
    });
}

void InteractionManager::stopCall() {
    m_executor.submit([this]() {
        if (m_client->isCommsEnabled()) {
            m_client->stopCommsCall();
        } else {
            m_userInterface->printCommsNotSupported();
        }
    });
}

void InteractionManager::muteCallToggle() {
    m_executor.submit([this]() {
        if (m_client->isCommsCallMuted()) {
            m_client->unmuteCommsCall();
            m_userInterface->printUnmuteCallScreen();
        } else {
            m_client->muteCommsCall();
            m_userInterface->printMuteCallScreen();
        }
    });
}

void InteractionManager::sendDtmf(avsCommon::sdkInterfaces::CallManagerInterface::DTMFTone dtmfTone) {
    m_executor.submit([this, dtmfTone]() {
        if (m_client->isCommsEnabled()) {
            m_client->sendDtmf(dtmfTone);
        } else {
            m_userInterface->printCommsNotSupported();
        }
    });
}

void InteractionManager::dtmfControl() {
    m_executor.submit([this]() { m_userInterface->printDtmfScreen(); });
}

void InteractionManager::errorDtmf() {
    m_executor.submit([this]() { m_userInterface->printDtmfErrorScreen(); });
}
#endif

void InteractionManager::onDialogUXStateChanged(DialogUXState state) {
    m_executor.submit([this, state]() {
        if (DialogUXState::LISTENING == state) {
            if (m_isMicOn && !m_micWrapper->isStreaming()) {
                m_micWrapper->startStreamingMicrophoneData();
            }
        } else {
            // reset tap-to-talk state
            m_isTapOccurring = false;

            // if wakeword is disabled and no call is occurring, turn off microphone
            if (!m_wakeWordAudioProvider && !m_isCallConnected && m_micWrapper->isStreaming()) {
                m_micWrapper->stopStreamingMicrophoneData();
            }
        }
    });
}

void InteractionManager::onCallStateChange(CallState state) {
    m_executor.submit([this, state]() {
        if (CallState::CALL_CONNECTED == state) {
            if (!m_micWrapper->isStreaming()) {
                m_micWrapper->startStreamingMicrophoneData();
            }
            m_isCallConnected = true;
        } else {
            // reset call state
            m_isCallConnected = false;

            // if wakeword is disabled, turn off microphone when call is not connected and tap is not occurring
            if (!m_wakeWordAudioProvider && !m_isTapOccurring && m_micWrapper->isStreaming()) {
                m_micWrapper->stopStreamingMicrophoneData();
            }
        }
    });
}

void InteractionManager::setSpeechConfirmation(settings::SpeechConfirmationSettingType value) {
    m_client->getSettingsManager()->setValue<settings::SPEECH_CONFIRMATION>(value);
}

void InteractionManager::setWakewordConfirmation(settings::WakeWordConfirmationSettingType value) {
    m_client->getSettingsManager()->setValue<settings::WAKEWORD_CONFIRMATION>(value);
}

void InteractionManager::setTimeZone(const std::string& value) {
    m_client->getSettingsManager()->setValue<settings::TIMEZONE>(value);
}

void InteractionManager::setLocale(const settings::DeviceLocales& value) {
    m_client->getSettingsManager()->setValue<settings::LOCALE>(value);
}

settings::types::NetworkInfo InteractionManager::getNetworkInfo() {
    auto netSettings = m_client->getSettingsManager()->getValue<settings::NETWORK_INFO>();
    return netSettings.second;
}

void InteractionManager::setNetworkInfo(const settings::types::NetworkInfo& value) {
    m_client->getSettingsManager()->setValue<settings::NETWORK_INFO>(value);
}

void InteractionManager::setAlarmVolumeRamp(bool enable) {
    m_client->getSettingsManager()->setValue<settings::ALARM_VOLUME_RAMP>(settings::types::toAlarmRamp(enable));
}

#ifdef POWER_CONTROLLER
void InteractionManager::setPowerState(const bool powerState) {
    m_powerControllerHandler->setPowerState(powerState);
}
#endif

#ifdef TOGGLE_CONTROLLER
void InteractionManager::setToggleState(const bool toggleState) {
    m_toggleControllerHandler->setToggleState(toggleState);
}
#endif

#ifdef RANGE_CONTROLLER
void InteractionManager::setRangeValue(const int rangeValue) {
    m_rangeControllerHandler->setRangeValue(rangeValue);
}
#endif

#ifdef MODE_CONTROLLER
void InteractionManager::setMode(const std::string mode) {
    m_modeControllerHandler->setMode(mode);
}
#endif

#ifdef ENABLE_PCC
void InteractionManager::phoneControl() {
    m_executor.submit([this]() { m_userInterface->printPhoneControlScreen(); });
}

void InteractionManager::callId() {
    m_executor.submit([this]() { m_userInterface->printCallIdScreen(); });
}

void InteractionManager::callerId() {
    m_executor.submit([this]() { m_userInterface->printCallerIdScreen(); });
}

void InteractionManager::sendCallActivated(const std::string& callId) {
    m_executor.submit([this, callId]() {
        if (m_phoneCaller) {
            m_phoneCaller->sendCallActivated(callId);
        }
    });
}
void InteractionManager::sendCallTerminated(const std::string& callId) {
    m_executor.submit([this, callId]() {
        if (m_phoneCaller) {
            m_phoneCaller->sendCallTerminated(callId);
        }
    });
}

void InteractionManager::sendCallFailed(const std::string& callId) {
    m_executor.submit([this, callId]() {
        if (m_phoneCaller) {
            m_phoneCaller->sendCallFailed(callId);
        }
    });
}

void InteractionManager::sendCallReceived(const std::string& callId, const std::string& callerId) {
    m_executor.submit([this, callId, callerId]() {
        if (m_phoneCaller) {
            m_phoneCaller->sendCallReceived(callId, callerId);
        }
    });
}

void InteractionManager::sendCallerIdReceived(const std::string& callId, const std::string& callerId) {
    m_executor.submit([this, callId, callerId]() {
        if (m_phoneCaller) {
            m_phoneCaller->sendCallerIdReceived(callId, callerId);
        }
    });
}

void InteractionManager::sendInboundRingingStarted(const std::string& callId) {
    m_executor.submit([this, callId]() {
        if (m_phoneCaller) {
            m_phoneCaller->sendInboundRingingStarted(callId);
        }
    });
}

void InteractionManager::sendOutboundCallRequested(const std::string& callId) {
    m_executor.submit([this, callId]() {
        if (m_phoneCaller) {
            m_phoneCaller->sendDialStarted(callId);
        }
    });
}

void InteractionManager::sendOutboundRingingStarted(const std::string& callId) {
    m_executor.submit([this, callId]() {
        if (m_phoneCaller) {
            m_phoneCaller->sendOutboundRingingStarted(callId);
        }
    });
}

void InteractionManager::sendSendDtmfSucceeded(const std::string& callId) {
    m_executor.submit([this, callId]() {
        if (m_phoneCaller) {
            m_phoneCaller->sendSendDtmfSucceeded(callId);
        }
    });
}

void InteractionManager::sendSendDtmfFailed(const std::string& callId) {
    m_executor.submit([this, callId]() {
        if (m_phoneCaller) {
            m_phoneCaller->sendSendDtmfFailed(callId);
        }
    });
}
#endif

#ifdef ENABLE_MCC
void InteractionManager::meetingControl() {
    m_executor.submit([this]() { m_userInterface->printMeetingControlScreen(); });
}

void InteractionManager::sessionId() {
    m_executor.submit([this]() { m_userInterface->printSessionIdScreen(); });
}

void InteractionManager::calendarItemsFile() {
    m_executor.submit([this]() { m_userInterface->printCalendarItemsScreen(); });
}

void InteractionManager::sendMeetingJoined(const std::string& sessionId) {
    m_executor.submit([this, sessionId]() {
        if (m_meetingClient) {
            m_meetingClient->sendMeetingJoined(sessionId);
        }
    });
}
void InteractionManager::sendMeetingEnded(const std::string& sessionId) {
    m_executor.submit([this, sessionId]() {
        if (m_meetingClient) {
            m_meetingClient->sendMeetingEnded(sessionId);
        }
    });
}

void InteractionManager::sendSetCurrentMeetingSession(const std::string& sessionId) {
    m_executor.submit([this, sessionId]() {
        if (m_meetingClient) {
            m_meetingClient->sendSetCurrentMeetingSession(sessionId);
        }
    });
}

void InteractionManager::sendClearCurrentMeetingSession() {
    m_executor.submit([this]() {
        if (m_meetingClient) {
            m_meetingClient->sendClearCurrentMeetingSession();
        }
    });
}

void InteractionManager::sendConferenceConfigurationChanged() {
    m_executor.submit([this]() {
        if (m_meetingClient) {
            m_meetingClient->sendConferenceConfigurationChanged();
        }
    });
}

void InteractionManager::sendMeetingClientErrorOccured(const std::string& sessionId) {
    m_executor.submit([this, sessionId]() {
        if (m_meetingClient) {
            m_meetingClient->sendMeetingClientErrorOccured(sessionId);
        }
    });
}

void InteractionManager::sendCalendarItemsRetrieved(const std::string& calendarItemsFile) {
    m_executor.submit([this, calendarItemsFile]() {
        if (m_calendarClient) {
            m_calendarClient->sendCalendarItemsRetrieved(calendarItemsFile);
        }
    });
}

void InteractionManager::sendCalendarClientErrorOccured() {
    m_executor.submit([this]() {
        if (m_calendarClient) {
            m_calendarClient->sendCalendarClientErrorOccured();
        }
    });
}
#endif

void InteractionManager::setDoNotDisturbMode(bool enable) {
    m_client->getSettingsManager()->setValue<settings::DO_NOT_DISTURB>(enable);
}

void InteractionManager::diagnosticsControl() {
    m_executor.submit([this]() { m_userInterface->printDiagnosticsScreen(); });
}

void InteractionManager::devicePropertiesControl() {
    m_executor.submit([this]() { m_userInterface->printDevicePropertiesScreen(); });
}

void InteractionManager::showDeviceProperties() {
    m_executor.submit([this]() {
        if (m_diagnostics) {
            auto deviceProperties = m_diagnostics->getDevicePropertyAggregator();
            if (deviceProperties) {
                m_userInterface->printAllDeviceProperties(deviceProperties->getAllDeviceProperties());
            }
        }
    });
}

void InteractionManager::audioInjectionControl() {
    m_executor.submit([this]() { m_userInterface->printAudioInjectionScreen(); });
}

void InteractionManager::injectWavFile(const std::string& absoluteFilePath) {
    m_executor.submit([this, absoluteFilePath]() {
        if (!m_diagnostics) {
            ACSDK_ERROR(LX("audioInjectionFailed").d("reason", "nullDiagnosticObject"));
            m_userInterface->printAudioInjectionFailureMessage();
            return;
        }
        auto audioInjector = m_diagnostics->getAudioInjector();
        if (!audioInjector) {
            ACSDK_ERROR(LX("audioInjectionFailed").d("reason", "nullAudioInjector"));
            m_userInterface->printAudioInjectionFailureMessage();
            return;
        }

        // Notify DefaultClient of tap-to-talk if wakeword is disabled.
        if (!m_wakeWordAudioProvider) {
            if (!m_client->notifyOfTapToTalk(m_tapToTalkAudioProvider).get()) {
                m_userInterface->printAudioInjectionFailureMessage();
                return;
            }
        }

        if (!audioInjector->injectAudio(absoluteFilePath)) {
            m_userInterface->printAudioInjectionFailureMessage();
            return;
        }
    });
}

void InteractionManager::deviceProtocolTraceControl() {
    m_executor.submit([this]() { m_userInterface->printDeviceProtocolTracerScreen(); });
}

void InteractionManager::printProtocolTrace() {
    m_executor.submit([this]() {
        if (m_diagnostics) {
            auto protocolTrace = m_diagnostics->getProtocolTracer();
            if (protocolTrace) {
                m_userInterface->printProtocolTrace(protocolTrace->getProtocolTrace());
            }
        }
    });
}

void InteractionManager::setProtocolTraceFlag(bool enabled) {
    m_executor.submit([this, enabled]() {
        if (m_diagnostics) {
            auto protocolTrace = m_diagnostics->getProtocolTracer();
            if (protocolTrace) {
                protocolTrace->setProtocolTraceFlag(enabled);
                m_userInterface->printProtocolTraceFlag(enabled);
            }
        }
    });
}

void InteractionManager::clearProtocolTrace() {
    m_executor.submit([this]() {
        if (m_diagnostics) {
            auto protocolTrace = m_diagnostics->getProtocolTracer();
            if (protocolTrace) {
                protocolTrace->clearTracedMessages();
            }
        }
    });
}

void InteractionManager::startMicrophone() {
    m_micWrapper->startStreamingMicrophoneData();
}

void InteractionManager::stopMicrophone() {
    m_micWrapper->stopStreamingMicrophoneData();
}

void InteractionManager::doShutdown() {
    m_client.reset();
}

}  // namespace sampleApp
}  // namespace alexaClientSDK
