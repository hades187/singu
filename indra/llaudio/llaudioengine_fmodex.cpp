/** 
 * @file audioengine_FMODEX.cpp
 * @brief Implementation of LLAudioEngine class abstracting the audio support as a FMOD 3D implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewergpl$
 * 
 * Copyright (c) 2002-2009, Linden Research, Inc.
 * 
 * Second Life Viewer Source Code
 * The source code in this file ("Source Code") is provided by Linden Lab
 * to you under the terms of the GNU General Public License, version 2.0
 * ("GPL"), unless you have obtained a separate licensing agreement
 * ("Other License"), formally executed by you and Linden Lab.  Terms of
 * the GPL can be found in doc/GPL-license.txt in this distribution, or
 * online at http://secondlifegrid.net/programs/open_source/licensing/gplv2
 * 
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file doc/FLOSS-exception.txt in this software distribution, or
 * online at
 * http://secondlifegrid.net/programs/open_source/licensing/flossexception
 * 
 * By copying, modifying or distributing this software, you acknowledge
 * that you have read and understood your obligations described above,
 * and agree to abide by those obligations.
 * 
 * ALL LINDEN LAB SOURCE CODE IS PROVIDED "AS IS." LINDEN LAB MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llstreamingaudio.h"
#include "llstreamingaudio_fmodex.h"

#include "llaudioengine_fmodex.h"
#include "lllistener_fmodex.h"

#include "llerror.h"
#include "llmath.h"
#include "llrand.h"

#include "fmod.hpp"
#include "fmod_errors.h"
#include "lldir.h"
#include "llapr.h"

#include "sound_ids.h"

#if LL_WINDOWS	//Some ugly code to make missing fmodex.dll not cause a fatal error.
#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#include <DelayImp.h>
#pragma comment(lib, "delayimp.lib")

bool attemptDelayLoad()
{
	__try
	{
#if defined(_WIN64)
		if( FAILED( __HrLoadAllImportsForDll( "fmodex64.dll" ) ) )
			return false;
#else
		if( FAILED( __HrLoadAllImportsForDll( "fmodex.dll" ) ) )
			return false;
#endif
	}
	__except( EXCEPTION_EXECUTE_HANDLER )
	{
		return false;
	}
	return true;
}
#endif

static bool sVerboseDebugging = false;

FMOD_RESULT F_CALLBACK windCallback(FMOD_DSP_STATE *dsp_state, float *inbuffer, float *outbuffer, unsigned int length, int inchannels, int outchannels);

FMOD::ChannelGroup *LLAudioEngine_FMODEX::mChannelGroups[LLAudioEngine::AUDIO_TYPE_COUNT] = {0};

//This class is designed to keep track of all sound<->channel assocations.
//Used to verify validity of sound and channel pointers, as well as catch cases were sounds
//are released with active channels still attached.
class CFMODSoundChecks
{
	typedef std::map<FMOD::Sound*,std::set<FMOD::Channel*> > active_sounds_t;
	typedef std::set<FMOD::Sound*> dead_sounds_t;
	typedef std::map<FMOD::Channel*,FMOD::Sound*> active_channels_t;
	typedef std::map<FMOD::Channel*,FMOD::Sound*> dead_channels_t;

	active_sounds_t mActiveSounds;
	dead_sounds_t mDeadSounds;
	active_channels_t mActiveChannels;
	dead_channels_t mDeadChannels;
public:
	enum STATUS
	{
		ACTIVE,
		DEAD,
		UNKNOWN
	};
	STATUS getPtrStatus(LLAudioChannel* channel)
	{
		if(!channel)
			return UNKNOWN;
		return getPtrStatus(dynamic_cast<LLAudioChannelFMODEX*>(channel)->mChannelp);
	}
	STATUS getPtrStatus(LLAudioBuffer* sound)
	{
		if(!sound)
			return UNKNOWN;
		return getPtrStatus(dynamic_cast<LLAudioBufferFMODEX*>(sound)->mSoundp);
	}
	STATUS getPtrStatus(FMOD::Channel* channel)
	{
		if(!channel)
			return UNKNOWN;
		else if(mActiveChannels.find(channel) != mActiveChannels.end())
			return ACTIVE;
		else if(mDeadChannels.find(channel) != mDeadChannels.end())
			return DEAD;
		return UNKNOWN;
	}
	STATUS getPtrStatus(FMOD::Sound* sound)
	{
		if(!sound)
			return UNKNOWN;
		if(mActiveSounds.find(sound) != mActiveSounds.end())
			return ACTIVE;
		else if(mDeadSounds.find(sound) != mDeadSounds.end())
			return DEAD;
		return UNKNOWN;
	}
	void addNewSound(FMOD::Sound* sound)
	{
		assertActiveState(sound,true,false);

		mDeadSounds.erase(sound);
		mActiveSounds.insert(std::make_pair(sound,std::set<FMOD::Channel*>()));
	}
	void removeSound(FMOD::Sound* sound)
	{
		assertActiveState(sound,true);

		active_sounds_t::const_iterator it = mActiveSounds.find(sound);
		llassert(it != mActiveSounds.end());
		if(it != mActiveSounds.end())
		{
			if(!it->second.empty())
			{
				LL_WARNS("AudioImpl")	<< "Removing sound " << sound << " with attached channels: \n";
				for(std::set<FMOD::Channel*>::iterator it2 = it->second.begin(); it2 != it->second.end();++it2)
				{
					switch(getPtrStatus(*it2))
					{
					case ACTIVE:
						LL_CONT << " Channel " << *it2 << " ACTIVE\n";
						break;
					case DEAD:
						LL_CONT << " Channel " << *it2 << " DEAD\n";
						break;
					default:
						LL_CONT << " Channel " << *it2 << " UNKNOWN\n";
					}
				}
				LL_CONT << LL_ENDL;
			}
			llassert(it->second.empty());
			mDeadSounds.insert(sound);
			mActiveSounds.erase(sound);
		}
	}
	void addNewChannelToSound(FMOD::Sound* sound,FMOD::Channel* channel)
	{
		assertActiveState(sound,true);
		assertActiveState(channel,true,false);

		mActiveSounds[sound].insert(channel);
		mActiveChannels.insert(std::make_pair(channel,sound));
	}
	void removeChannel(FMOD::Channel* channel)
	{
		assertActiveState(channel,true);

		active_channels_t::const_iterator it = mActiveChannels.find(channel);
		llassert(it != mActiveChannels.end());
		if(it != mActiveChannels.end())
		{
#ifdef SHOW_ASSERT
			STATUS status = getPtrStatus(it->second);
			llassert(status != DEAD);
			llassert(status != UNKNOWN);
#endif

			active_sounds_t::iterator it2 = mActiveSounds.find(it->second);
			llassert(it2 != mActiveSounds.end());
			if(it2 != mActiveSounds.end())
			{
				it2->second.erase(channel);
			}
			mDeadChannels.insert(*it);
			mActiveChannels.erase(channel);
		}
	}

	template <typename T>
	void assertActiveState(T ptr, bool try_log=false, bool active=true)
	{
#ifndef SHOW_ASSERT
		if(try_log && sVerboseDebugging)
#endif
		{
			CFMODSoundChecks::STATUS chan = getPtrStatus(ptr);
			if(try_log && sVerboseDebugging)
			{
				if(active)
				{
					if(chan == CFMODSoundChecks::DEAD)
						LL_WARNS("AudioImpl") << __FUNCTION__ << ": Using unexpectedly dead " << typeid(T*).name() << " " << ptr << LL_ENDL;
					else if(chan == CFMODSoundChecks::UNKNOWN)
						LL_WARNS("AudioImpl") << __FUNCTION__ << ": Using unexpectedly unknown " << typeid(T*).name() << " " << ptr << LL_ENDL;
				}
				else if(chan == CFMODSoundChecks::ACTIVE)
						LL_WARNS("AudioImpl") << __FUNCTION__ << ": Using unexpectedly active " << typeid(T*).name() << " " << ptr << LL_ENDL;
			}
			llassert( active == (chan == CFMODSoundChecks::ACTIVE) );
		}
	}
} gSoundCheck;

LLAudioEngine_FMODEX::LLAudioEngine_FMODEX(bool enable_profiler, bool verbose_debugging)
{
	sVerboseDebugging = verbose_debugging;
	mInited = false;
	mWindGen = NULL;
	mWindDSP = NULL;
	mSystem = NULL;
	mEnableProfiler = enable_profiler;
}


LLAudioEngine_FMODEX::~LLAudioEngine_FMODEX()
{
}


inline bool Check_FMOD_Error(FMOD_RESULT result, const char *string)
{
	if(result == FMOD_OK)
		return false;
	LL_WARNS("AudioImpl") << string << " Error: " << FMOD_ErrorString(result) << LL_ENDL;
	return true;
}

void* F_STDCALL decode_alloc(unsigned int size, FMOD_MEMORY_TYPE type, const char *sourcestr)
{
	if(type & FMOD_MEMORY_STREAM_DECODE)
	{
		LL_INFOS("AudioImpl") << "Decode buffer size: " << size << LL_ENDL;
	}
	else if(type & FMOD_MEMORY_STREAM_FILE)
	{
		LL_INFOS("AudioImpl") << "Stream buffer size: " << size << LL_ENDL;
	}
	return new char[size];
}
void* F_STDCALL decode_realloc(void *ptr, unsigned int size, FMOD_MEMORY_TYPE type, const char *sourcestr)
{
	memset(ptr,0,size);
	return ptr;
}
void F_STDCALL decode_dealloc(void *ptr, FMOD_MEMORY_TYPE type, const char *sourcestr)
{
	delete[] (char*)ptr;
}

bool LLAudioEngine_FMODEX::init(const S32 num_channels, void* userdata)
{

#if LL_WINDOWS
	if(!attemptDelayLoad())
		return false;
#endif

	U32 version = 0;
	FMOD_RESULT result;

	LL_DEBUGS("AppInit") << "LLAudioEngine_FMODEX::init() initializing FMOD" << LL_ENDL;

	//result = FMOD::Memory_Initialize(NULL, 0, &decode_alloc, &decode_realloc, &decode_dealloc, FMOD_MEMORY_STREAM_DECODE | FMOD_MEMORY_STREAM_FILE);
	//if(Check_FMOD_Error(result, "FMOD::Memory_Initialize"))
	//	return false;

	result = FMOD::System_Create(&mSystem);
	if(Check_FMOD_Error(result, "FMOD::System_Create"))
		return false;

	//will call LLAudioEngine_FMODEX::allocateListener, which needs a valid mSystem pointer.
	LLAudioEngine::init(num_channels, userdata);	
	
	result = mSystem->getVersion(&version);
	Check_FMOD_Error(result, "FMOD::System::getVersion");

	if (version < FMOD_VERSION)
	{
		LL_WARNS("AppInit") << "Error : You are using the wrong FMOD Ex version (" << version
			<< ")!  You should be using FMOD Ex" << FMOD_VERSION << LL_ENDL;
	}

//	result = mSystem->setSoftwareFormat(44100, FMOD_SOUND_FORMAT_PCM16, 0, 0, FMOD_DSP_RESAMPLER_LINEAR);
//	Check_FMOD_Error(result,"FMOD::System::setSoftwareFormat");

	// In this case, all sounds, PLUS wind and stream will be software.
	result = mSystem->setSoftwareChannels(num_channels + 2);
	Check_FMOD_Error(result,"FMOD::System::setSoftwareChannels");

	U32 fmod_flags = FMOD_INIT_NORMAL;
	if(mEnableProfiler)
	{
		fmod_flags |= FMOD_INIT_ENABLE_PROFILE;
	}

#if LL_LINUX
	bool audio_ok = false;

	if (!audio_ok)
	{
		if (NULL == getenv("LL_BAD_FMOD_PULSEAUDIO")) /*Flawfinder: ignore*/
		{
			LL_DEBUGS("AppInit") << "Trying PulseAudio audio output..." << LL_ENDL;
			if((result = mSystem->setOutput(FMOD_OUTPUTTYPE_PULSEAUDIO)) == FMOD_OK &&
				(result = mSystem->init(num_channels + 2, fmod_flags, 0)) == FMOD_OK)
			{
				LL_DEBUGS("AppInit") << "PulseAudio output initialized OKAY"	<< LL_ENDL;
				audio_ok = true;
			}
			else 
			{
				Check_FMOD_Error(result, "PulseAudio audio output FAILED to initialize");
			}
		} 
		else 
		{
			LL_DEBUGS("AppInit") << "PulseAudio audio output SKIPPED" << LL_ENDL;
		}	
	}
	if (!audio_ok)
	{
		if (NULL == getenv("LL_BAD_FMOD_ALSA"))		/*Flawfinder: ignore*/
		{
			LL_DEBUGS("AppInit") << "Trying ALSA audio output..." << LL_ENDL;
			if((result = mSystem->setOutput(FMOD_OUTPUTTYPE_ALSA)) == FMOD_OK &&
			    (result = mSystem->init(num_channels + 2, fmod_flags, 0)) == FMOD_OK)
			{
				LL_DEBUGS("AppInit") << "ALSA audio output initialized OKAY" << LL_ENDL;
				audio_ok = true;
			} 
			else 
			{
				Check_FMOD_Error(result, "ALSA audio output FAILED to initialize");
			}
		} 
		else 
		{
			LL_DEBUGS("AppInit") << "ALSA audio output SKIPPED" << LL_ENDL;
		}
	}
	if (!audio_ok)
	{
		if (NULL == getenv("LL_BAD_FMOD_OSS")) 	 /*Flawfinder: ignore*/
		{
			LL_DEBUGS("AppInit") << "Trying OSS audio output..." << LL_ENDL;
			if((result = mSystem->setOutput(FMOD_OUTPUTTYPE_OSS)) == FMOD_OK &&
			    (result = mSystem->init(num_channels + 2, fmod_flags, 0)) == FMOD_OK)
			{
				LL_DEBUGS("AppInit") << "OSS audio output initialized OKAY" << LL_ENDL;
				audio_ok = true;
			}
			else
			{
				Check_FMOD_Error(result, "OSS audio output FAILED to initialize");
			}
		}
		else 
		{
			LL_DEBUGS("AppInit") << "OSS audio output SKIPPED" << LL_ENDL;
		}
	}
	if (!audio_ok)
	{
		LL_WARNS("AppInit") << "Overall audio init failure." << LL_ENDL;
		return false;
	}

	// We're interested in logging which output method we
	// ended up with, for QA purposes.
	FMOD_OUTPUTTYPE output_type;
	if(!Check_FMOD_Error(mSystem->getOutput(&output_type), "FMOD::System::getOutput"))
	{
		switch (output_type)
		{
			case FMOD_OUTPUTTYPE_NOSOUND: 
				LL_INFOS("AppInit") << "Audio output: NoSound" << LL_ENDL; break;
			case FMOD_OUTPUTTYPE_PULSEAUDIO:	
				LL_INFOS("AppInit") << "Audio output: PulseAudio" << LL_ENDL; break;
			case FMOD_OUTPUTTYPE_ALSA: 
				LL_INFOS("AppInit") << "Audio output: ALSA" << LL_ENDL; break;
			case FMOD_OUTPUTTYPE_OSS:	
				LL_INFOS("AppInit") << "Audio output: OSS" << LL_ENDL; break;
			default:
				LL_INFOS("AppInit") << "Audio output: Unknown!" << LL_ENDL; break;
		};
	}
#else // LL_LINUX

	// initialize the FMOD engine
	result = mSystem->init( num_channels + 2, fmod_flags, 0);
	if (result == FMOD_ERR_OUTPUT_CREATEBUFFER)
	{
		/*
		Ok, the speaker mode selected isn't supported by this soundcard. Switch it
		back to stereo...
		*/
		result = mSystem->setSpeakerMode(FMOD_SPEAKERMODE_STEREO);
		Check_FMOD_Error(result,"Error falling back to stereo mode");
		/*
		... and re-init.
		*/
		result = mSystem->init( num_channels + 2, fmod_flags, 0);
	}
	if(Check_FMOD_Error(result, "Error initializing FMOD Ex"))
		return false;
#endif

	if (mEnableProfiler)
	{
		Check_FMOD_Error(mSystem->createChannelGroup("None", &mChannelGroups[AUDIO_TYPE_NONE]), "FMOD::System::createChannelGroup");
		Check_FMOD_Error(mSystem->createChannelGroup("SFX", &mChannelGroups[AUDIO_TYPE_SFX]), "FMOD::System::createChannelGroup");
		Check_FMOD_Error(mSystem->createChannelGroup("UI", &mChannelGroups[AUDIO_TYPE_UI]), "FMOD::System::createChannelGroup");
		Check_FMOD_Error(mSystem->createChannelGroup("Ambient", &mChannelGroups[AUDIO_TYPE_AMBIENT]), "FMOD::System::createChannelGroup");
	}

	// set up our favourite FMOD-native streaming audio implementation if none has already been added
	if (!getStreamingAudioImpl()) // no existing implementation added
		setStreamingAudioImpl(new LLStreamingAudio_FMODEX(mSystem));

	LL_INFOS("AppInit") << "LLAudioEngine_FMODEX::init() FMOD Ex initialized correctly" << LL_ENDL;

	int r_numbuffers, r_samplerate, r_channels, r_bits;
	unsigned int r_bufferlength;
	char r_name[256];
	FMOD_SPEAKERMODE speaker_mode;
	if (!Check_FMOD_Error(mSystem->getDSPBufferSize(&r_bufferlength, &r_numbuffers), "FMOD::System::getDSPBufferSize") &&
		!Check_FMOD_Error(mSystem->getSoftwareFormat(&r_samplerate, NULL, &r_channels, NULL, NULL, &r_bits), "FMOD::System::getSoftwareFormat") &&
		!Check_FMOD_Error(mSystem->getDriverInfo(0, r_name, 255, 0), "FMOD::System::getDriverInfo") &&
		!Check_FMOD_Error(mSystem->getSpeakerMode(&speaker_mode), "FMOD::System::getSpeakerMode"))
	{
		std::string speaker_mode_str = "unknown";
		switch(speaker_mode)
		{
			#define SPEAKER_MODE_CASE(MODE) case MODE: speaker_mode_str = #MODE; break;
			SPEAKER_MODE_CASE(FMOD_SPEAKERMODE_RAW)
			SPEAKER_MODE_CASE(FMOD_SPEAKERMODE_MONO)
			SPEAKER_MODE_CASE(FMOD_SPEAKERMODE_STEREO)
			SPEAKER_MODE_CASE(FMOD_SPEAKERMODE_QUAD)
			SPEAKER_MODE_CASE(FMOD_SPEAKERMODE_SURROUND)
			SPEAKER_MODE_CASE(FMOD_SPEAKERMODE_5POINT1)
			SPEAKER_MODE_CASE(FMOD_SPEAKERMODE_7POINT1)
			SPEAKER_MODE_CASE(FMOD_SPEAKERMODE_SRS5_1_MATRIX)
			SPEAKER_MODE_CASE(FMOD_SPEAKERMODE_MYEARS)
			default:;
			#undef SPEAKER_MODE_CASE
		}

		r_name[255] = '\0';
		int latency = 1000.0 * r_bufferlength * r_numbuffers /r_samplerate;

		LL_INFOS("AppInit") << "FMOD device: "<< r_name << "\n"
			<< "Output mode: "<< speaker_mode_str << "\n"
			<< "FMOD Ex parameters: " << r_samplerate << " Hz * " << r_channels << " * " <<r_bits <<" bit\n"
			<< "\tbuffer " << r_bufferlength << " * " << r_numbuffers << " (" << latency <<"ms)" << LL_ENDL;
	}
	else
	{
		LL_WARNS("AppInit") << "Failed to retrieve FMOD device info!" << LL_ENDL;
	}
	mInited = true;

	return true;
}


std::string LLAudioEngine_FMODEX::getDriverName(bool verbose)
{
	llassert_always(mSystem);
	if (verbose)
	{
		U32 version;
		if(!Check_FMOD_Error(mSystem->getVersion(&version), "FMOD::System::getVersion"))
		{
			return llformat("FMOD Ex %1x.%02x.%02x", version >> 16, version >> 8 & 0x000000FF, version & 0x000000FF);
		}
	}
	return "FMODEx";
}


void LLAudioEngine_FMODEX::allocateListener(void)
{	
	mListenerp = (LLListener *) new LLListener_FMODEX(mSystem);
	if (!mListenerp)
	{
		LL_WARNS("AudioImpl") << "Listener creation failed" << LL_ENDL;
	}
}


void LLAudioEngine_FMODEX::shutdown()
{
	LL_INFOS("AudioImpl") << "About to LLAudioEngine::shutdown()" << LL_ENDL;
	LLAudioEngine::shutdown();
	
	LL_INFOS("AudioImpl") << "LLAudioEngine_FMODEX::shutdown() closing FMOD Ex" << LL_ENDL;
	if ( mSystem ) // speculative fix for MAINT-2657
	{
		LL_INFOS("AudioImpl") << "LLAudioEngine_FMODEX::shutdown() Requesting FMOD Ex system closure" << LL_ENDL;
		Check_FMOD_Error(mSystem->close(), "FMOD::System::close");
		LL_INFOS("AudioImpl") << "LLAudioEngine_FMODEX::shutdown() Requesting FMOD Ex system release" << LL_ENDL;
		Check_FMOD_Error(mSystem->release(), "FMOD::System::release");
	}
	LL_INFOS("AudioImpl") << "LLAudioEngine_FMODEX::shutdown() done closing FMOD Ex" << LL_ENDL;

	delete mListenerp;
	mListenerp = NULL;
}


LLAudioBuffer * LLAudioEngine_FMODEX::createBuffer()
{
	return new LLAudioBufferFMODEX(mSystem);
}


LLAudioChannel * LLAudioEngine_FMODEX::createChannel()
{
	return new LLAudioChannelFMODEX(mSystem);
}

bool LLAudioEngine_FMODEX::initWind()
{
	mNextWindUpdate = 0.0;

	cleanupWind();


	FMOD_DSP_DESCRIPTION dspdesc;
	memset(&dspdesc, 0, sizeof(FMOD_DSP_DESCRIPTION));	//Set everything to zero
	strncpy(dspdesc.name,"Wind Unit", sizeof(dspdesc.name));	//Set name to "Wind Unit"
	dspdesc.channels=2;
	dspdesc.read = &windCallback; //Assign callback.
	if(Check_FMOD_Error(mSystem->createDSP(&dspdesc, &mWindDSP), "FMOD::createDSP") || !mWindDSP)
		return false;
	
	float frequency = 44100;
	if (!Check_FMOD_Error(mWindDSP->getDefaults(&frequency,0,0,0), "FMOD::DSP::getDefaults"))
	{
		mWindGen = new LLWindGen<MIXBUFFERFORMAT>((U32)frequency);
		if (!Check_FMOD_Error(mWindDSP->setUserData((void*)mWindGen), "FMOD::DSP::setUserData") &&
			!Check_FMOD_Error(mSystem->playDSP(FMOD_CHANNEL_FREE, mWindDSP, false, 0), "FMOD::System::playDSP"))
			return true;	//Success
	}

	cleanupWind();
	return false;
}


void LLAudioEngine_FMODEX::cleanupWind()
{
	if (mWindDSP)
	{
		Check_FMOD_Error(mWindDSP->remove(), "FMOD::DSP::remove");
		Check_FMOD_Error(mWindDSP->release(), "FMOD::DSP::release");
		mWindDSP = NULL;
	}

	delete mWindGen;
	mWindGen = NULL;
}


//-----------------------------------------------------------------------
void LLAudioEngine_FMODEX::updateWind(LLVector3 wind_vec, F32 camera_height_above_water)
{
	LLVector3 wind_pos;
	F64 pitch;
	F64 center_freq;

	if (!mEnableWind)
	{
		return;
	}
	
	if (mWindUpdateTimer.checkExpirationAndReset(LL_WIND_UPDATE_INTERVAL))
	{
		
		// wind comes in as Linden coordinate (+X = forward, +Y = left, +Z = up)
		// need to convert this to the conventional orientation DS3D and OpenAL use
		// where +X = right, +Y = up, +Z = backwards

		wind_vec.setVec(-wind_vec.mV[1], wind_vec.mV[2], -wind_vec.mV[0]);

		// cerr << "Wind update" << endl;

		pitch = 1.0 + mapWindVecToPitch(wind_vec);
		center_freq = 80.0 * pow(pitch,2.5*(mapWindVecToGain(wind_vec)+1.0));
		
		mWindGen->mTargetFreq = (F32)center_freq;
		mWindGen->mTargetGain = (F32)mapWindVecToGain(wind_vec) * mMaxWindGain;
		mWindGen->mTargetPanGainR = (F32)mapWindVecToPan(wind_vec);
  	}
}

//-----------------------------------------------------------------------
void LLAudioEngine_FMODEX::setInternalGain(F32 gain)
{
	if (!mInited)
	{
		return;
	}

	gain = llclamp( gain, 0.0f, 1.0f );

	FMOD::ChannelGroup *master_group;
	if(Check_FMOD_Error(mSystem->getMasterChannelGroup(&master_group), "FMOD::System::getMasterChannelGroup"))
		return;
	master_group->setVolume(gain);

	LLStreamingAudioInterface *saimpl = getStreamingAudioImpl();
	if ( saimpl )
	{
		// fmod likes its streaming audio channel gain re-asserted after
		// master volume change.
		saimpl->setGain(saimpl->getGain());
	}
}

//
// LLAudioChannelFMODEX implementation
//

LLAudioChannelFMODEX::LLAudioChannelFMODEX(FMOD::System *system) : LLAudioChannel(), mSystemp(system), mChannelp(NULL), mLastSamplePos(0)
{
}


LLAudioChannelFMODEX::~LLAudioChannelFMODEX()
{
	if(sVerboseDebugging)
		LL_DEBUGS("AudioImpl") << "Destructing Audio Channel. mChannelp = " << mChannelp << LL_ENDL;

	cleanup();
}

static FMOD_RESULT F_CALLBACK channel_callback(FMOD_CHANNEL *channel, FMOD_CHANNEL_CALLBACKTYPE type, void *commanddata1, void *commanddata2)
{
	if(type == FMOD_CHANNEL_CALLBACKTYPE_END)
	{
		FMOD::Channel* chan = reinterpret_cast<FMOD::Channel*>(channel);
		LLAudioChannelFMODEX* audio_channel = NULL;
		chan->getUserData((void**)&audio_channel);
		if(audio_channel)
		{
			audio_channel->onRelease();
		}
	}
	return FMOD_OK;
}

void LLAudioChannelFMODEX::onRelease()
{
	llassert(mChannelp);
	if(sVerboseDebugging)
		LL_DEBUGS("AudioImpl") << "Fmod signaled channel release for channel  " << mChannelp << LL_ENDL;
	gSoundCheck.removeChannel(mChannelp);
	mChannelp = NULL;	//Null out channel here so cleanup doesn't try to redundantly stop it.
	cleanup();
}

bool LLAudioChannelFMODEX::updateBuffer()
{
	if (LLAudioChannel::updateBuffer())
	{
		// Base class update returned true, which means the channel buffer was changed, and not is null.

		// Grab the FMOD sample associated with the buffer
		FMOD::Sound *soundp = ((LLAudioBufferFMODEX*)mCurrentBufferp)->getSound();
		if (!soundp)
		{
			// This is bad, there should ALWAYS be a sound associated with a legit
			// buffer.
			LL_ERRS("AudioImpl") << "No FMOD sound!" << LL_ENDL;
			return false;
		}

		// Actually play the sound.  Start it off paused so we can do all the necessary
		// setup.
		if(!mChannelp)
		{
			FMOD_RESULT result = getSystem()->playSound(FMOD_CHANNEL_FREE, soundp, true, &mChannelp);
			Check_FMOD_Error(result, "FMOD::System::playSound");
			if(result == FMOD_OK)
			{
				if(sVerboseDebugging)
					LL_DEBUGS("AudioImpl") << "Created channel " << mChannelp << " for sound " << soundp << LL_ENDL;

				gSoundCheck.addNewChannelToSound(soundp,mChannelp);
				mChannelp->setCallback(&channel_callback);
				mChannelp->setUserData(this);
			}
		}
	}

	// If we have a source for the channel, we need to update its gain.
	if (mCurrentSourcep && mChannelp)
	{
		FMOD_RESULT result;

		gSoundCheck.assertActiveState(this);
		result = mChannelp->setVolume(getSecondaryGain() * mCurrentSourcep->getGain());
		Check_FMOD_Error(result, "FMOD::Channel::setVolume");
		result = mChannelp->setMode(mCurrentSourcep->isLoop() ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF);
		Check_FMOD_Error(result, "FMOD::Channel::setMode");
	}

	return true;
}


void LLAudioChannelFMODEX::update3DPosition()
{
	if (!mChannelp)
	{
		// We're not actually a live channel (i.e., we're not playing back anything)
		return;
	}

	LLAudioBufferFMODEX  *bufferp = (LLAudioBufferFMODEX  *)mCurrentBufferp;
	if (!bufferp)
	{
		// We don't have a buffer associated with us (should really have been picked up
		// by the above if.
		return;
	}

	gSoundCheck.assertActiveState(this);

	if (mCurrentSourcep->isAmbient())
	{
		// Ambient sound, don't need to do any positional updates.
		set3DMode(false);
	}
	else
	{
		// Localized sound.  Update the position and velocity of the sound.
		set3DMode(true);

		LLVector3 float_pos;
		float_pos.setVec(mCurrentSourcep->getPositionGlobal());
		FMOD_RESULT result = mChannelp->set3DAttributes((FMOD_VECTOR*)float_pos.mV, (FMOD_VECTOR*)mCurrentSourcep->getVelocity().mV);
		Check_FMOD_Error(result, "FMOD::Channel::set3DAttributes");
	}
}


void LLAudioChannelFMODEX::updateLoop()
{
	if (!mChannelp)
	{
		// May want to clear up the loop/sample counters.
		return;
	}

	gSoundCheck.assertActiveState(this);

	//
	// Hack:  We keep track of whether we looped or not by seeing when the
	// sample position looks like it's going backwards.  Not reliable; may
	// yield false negatives.
	//
	U32 cur_pos;
	Check_FMOD_Error(mChannelp->getPosition(&cur_pos,FMOD_TIMEUNIT_PCMBYTES),"FMOD::Channel::getPosition");

	if (cur_pos < (U32)mLastSamplePos)
	{
		mLoopedThisFrame = true;
	}
	mLastSamplePos = cur_pos;
}


void LLAudioChannelFMODEX::cleanup()
{
	LLAudioChannel::cleanup();

	if (!mChannelp)
	{
		llassert(mCurrentBufferp == NULL);
		//LL_INFOS("AudioImpl") << "Aborting cleanup with no channel handle." << LL_ENDL;
		return;
	}

	if(sVerboseDebugging)
		LL_DEBUGS("AudioImpl") << "Stopping channel " << mChannelp << LL_ENDL;

	gSoundCheck.removeChannel(mChannelp);
	mChannelp->setCallback(NULL);
	Check_FMOD_Error(mChannelp->stop(),"FMOD::Channel::stop");

	mChannelp = NULL;
	mLastSamplePos = 0;
}


void LLAudioChannelFMODEX::play()
{
	if (!mChannelp)
	{
		LL_WARNS("AudioImpl") << "Playing without a channel handle, aborting" << LL_ENDL;
		return;
	}

	gSoundCheck.assertActiveState(this,true);

	bool paused=true;
	Check_FMOD_Error(mChannelp->getPaused(&paused), "FMOD::Channel::getPaused");
	if(!paused)
	{
		Check_FMOD_Error(mChannelp->setPaused(true), "FMOD::Channel::setPaused");
		Check_FMOD_Error(mChannelp->setPosition(0,FMOD_TIMEUNIT_PCMBYTES), "FMOD::Channel::setPosition");
	}
	Check_FMOD_Error(mChannelp->setPaused(false), "FMOD::Channel::setPaused");

	if(sVerboseDebugging)
		LL_DEBUGS("AudioImpl") << "Playing channel " << mChannelp << LL_ENDL;

	getSource()->setPlayedOnce(true);

	if(LLAudioEngine_FMODEX::mChannelGroups[getSource()->getType()])
		Check_FMOD_Error(mChannelp->setChannelGroup(LLAudioEngine_FMODEX::mChannelGroups[getSource()->getType()]),"FMOD::Channel::setChannelGroup");
}


void LLAudioChannelFMODEX::playSynced(LLAudioChannel *channelp)
{
	LLAudioChannelFMODEX *fmod_channelp = (LLAudioChannelFMODEX*)channelp;
	if (!(fmod_channelp->mChannelp && mChannelp))
	{
		// Don't have channels allocated to both the master and the slave
		return;
	}

	gSoundCheck.assertActiveState(this,true);

	U32 cur_pos;
	if(Check_FMOD_Error(fmod_channelp->mChannelp->getPosition(&cur_pos,FMOD_TIMEUNIT_PCMBYTES), "Unable to retrieve current position"))
		return;

	cur_pos %= mCurrentBufferp->getLength();
	
	// Try to match the position of our sync master
	Check_FMOD_Error(mChannelp->setPosition(cur_pos,FMOD_TIMEUNIT_PCMBYTES),"Unable to set current position");

	// Start us playing
	play();
}


bool LLAudioChannelFMODEX::isPlaying()
{
	if (!mChannelp)
	{
		return false;
	}

	gSoundCheck.assertActiveState(this);

	bool paused, playing;
	Check_FMOD_Error(mChannelp->getPaused(&paused),"FMOD::Channel::getPaused");
	Check_FMOD_Error(mChannelp->isPlaying(&playing),"FMOD::Channel::isPlaying");
	return !paused && playing;
}


//
// LLAudioChannelFMODEX implementation
//


LLAudioBufferFMODEX::LLAudioBufferFMODEX(FMOD::System *system) : LLAudioBuffer(), mSystemp(system), mSoundp(NULL)
{
}


LLAudioBufferFMODEX::~LLAudioBufferFMODEX()
{
	if(mSoundp)
	{
		if(sVerboseDebugging)
			LL_DEBUGS("AudioImpl") << "Release sound " << mSoundp << LL_ENDL;

		gSoundCheck.removeSound(mSoundp);
		Check_FMOD_Error(mSoundp->release(),"FMOD::Sound::Release");
		mSoundp = NULL;
	}
}


bool LLAudioBufferFMODEX::loadWAV(const std::string& filename)
{
	// Try to open a wav file from disk.  This will eventually go away, as we don't
	// really want to block doing this.
	if (filename.empty())
	{
		// invalid filename, abort.
		return false;
	}

	if (!LLAPRFile::isExist(filename, LL_APR_RPB))
	{
		// File not found, abort.
		return false;
	}
	
	if (mSoundp)
	{
		gSoundCheck.removeSound(mSoundp);
		// If there's already something loaded in this buffer, clean it up.
		Check_FMOD_Error(mSoundp->release(),"FMOD::Sound::release");
		mSoundp = NULL;
	}

	FMOD_MODE base_mode = FMOD_LOOP_NORMAL | FMOD_SOFTWARE;
	FMOD_CREATESOUNDEXINFO exinfo;
	memset(&exinfo,0,sizeof(exinfo));
	exinfo.cbsize = sizeof(exinfo);
	exinfo.suggestedsoundtype = FMOD_SOUND_TYPE_WAV;	//Hint to speed up loading.
	// Load up the wav file into an fmod sample
#if LL_WINDOWS
	FMOD_RESULT result = getSystem()->createSound((const char*)utf8str_to_utf16str(filename).c_str(), base_mode | FMOD_UNICODE, &exinfo, &mSoundp);
#else
	FMOD_RESULT result = getSystem()->createSound(filename.c_str(), base_mode, &exinfo, &mSoundp);
#endif

	if (result != FMOD_OK)
	{
		// We failed to load the file for some reason.
		LL_WARNS("AudioImpl") << "Could not load data '" << filename << "': " << FMOD_ErrorString(result) << LL_ENDL;

		//
		// If we EVER want to load wav files provided by end users, we need
		// to rethink this!
		//
		// file is probably corrupt - remove it.
		LLFile::remove(filename);
		return false;
	}

	gSoundCheck.addNewSound(mSoundp);

	// Everything went well, return true
	return true;
}


U32 LLAudioBufferFMODEX::getLength()
{
	if (!mSoundp)
	{
		return 0;
	}

	gSoundCheck.assertActiveState(this);
	U32 length;
	Check_FMOD_Error(mSoundp->getLength(&length, FMOD_TIMEUNIT_PCMBYTES),"FMOD::Sound::getLength");
	return length;
}


void LLAudioChannelFMODEX::set3DMode(bool use3d)
{
	gSoundCheck.assertActiveState(this);

	FMOD_MODE current_mode;
	if(Check_FMOD_Error(mChannelp->getMode(&current_mode),"FMOD::Channel::getMode"))
		return;
	FMOD_MODE new_mode = current_mode;	
	new_mode &= ~(use3d ? FMOD_2D : FMOD_3D);
	new_mode |= use3d ? FMOD_3D : FMOD_2D;

	if(current_mode != new_mode)
	{
		Check_FMOD_Error(mChannelp->setMode(new_mode),"FMOD::Channel::setMode");
	}
}


FMOD_RESULT F_CALLBACK windCallback(FMOD_DSP_STATE *dsp_state, float *originalbuffer, float *newbuffer, unsigned int length, int inchannels, int outchannels)
{
	// originalbuffer = fmod's original mixbuffer.
	// newbuffer = the buffer passed from the previous DSP unit.
	// length = length in samples at this mix time.
	// userdata = user parameter passed through in FSOUND_DSP_Create.
	
	LLWindGen<LLAudioEngine_FMODEX::MIXBUFFERFORMAT> *windgen;
	FMOD::DSP *thisdsp = (FMOD::DSP *)dsp_state->instance;

	thisdsp->getUserData((void **)&windgen);
	
	if (windgen)
		windgen->windGenerate((LLAudioEngine_FMODEX::MIXBUFFERFORMAT *)newbuffer, length);

	return FMOD_OK;
}
