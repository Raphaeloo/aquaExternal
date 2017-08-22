#include <iostream>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <X11/keysym.h>
#include <X11/keysymdef.h>

#include <X11/extensions/XTest.h>

#include <unistd.h>
#include <string>
#include <cstdio>
#include <cstdlib>

#include <chrono>
#include <thread>
#include <libconfig.h++>

#include "remote.h"
#include "cheat.h"
#include "logger.h"

using namespace std;
using namespace libconfig;

Config cfg;

string getConfigValue(string property) 
{
	try 
	{
		string name = cfg.lookup(property);
		return name;
	} 
	catch (const SettingNotFoundException &nfex) 
	{
		stringstream ss;
		ss << "Cannot find property: '" << property << "' in config.cfg file";
		Logger::error(ss.str());
	}

	return NULL;
}

bool dumpOffsets = 0;

int main() 
{
	Logger::init();

	if (getuid() != 0) 
	{
		Logger::error(string("You need to be ") + UNDERLINE + "root");
		return 0;
	}

	Display* display = XOpenDisplay(0);

	try 
		cfg.readFile("config.cfg");

	catch (const FileIOException &fioex) 
	{
		Logger::error("Error reading config file!");
	} 
	
	catch (const ParseException &pex) 
	{
		stringstream ss;
		ss << "Parsing error at " << pex.getFile() << ":" << pex.getLine() << " - " << pex.getError();
		Logger::error(ss.str());
	}

	int keycodeGlow =  XKeysymToKeycode(display, XStringToKeysym(getConfigValue("glowKey").c_str()));
	int keycodeRCS =  XKeysymToKeycode(display, XStringToKeysym(getConfigValue("rcsKey").c_str()));
	int keycodeTrigger =  XKeysymToKeycode(display, XStringToKeysym(getConfigValue("triggerKey").c_str()));

	double enemyRed = (::atof(getConfigValue("glowRed").c_str()) / 255);
	double enemyGreen = (::atof(getConfigValue("glowGreen").c_str()) / 255);
	double enemyBlue = (::atof(getConfigValue("glowBlue").c_str()) / 255);
	double enemyAlpha = ::atof(getConfigValue("glowAlpha").c_str());
	
	bool fullBloom = ::atof(getConfigValue("fullBloom").c_str());
	
	bool healthBased = ::atof(getConfigValue("healthBased").c_str());
	
	bool rainbowOn = ::atof(getConfigValue("rainbow").c_str());
	
	float sensitivity = ::atof(getConfigValue("sensitivity").c_str());
	
	float m_pitch = ::atof(getConfigValue("m_pitch").c_str());
	float m_yaw = ::atof(getConfigValue("m_yaw").c_str());
	
	bool visCheck = ::atof(getConfigValue("visibilityCheck").c_str());
	
	bool disablePostProcessing = ::atof(getConfigValue("disablePostProcessing").c_str());
	
	bool musicKitEnabled = ::atof(getConfigValue("musicKitChangerEnabled").c_str());
	int musicKitID = ::atof(getConfigValue("musicKitID").c_str());
	
	bool iFovEnabled = ::atof(getConfigValue("fovEnabled").c_str());
	int iFov = ::atof(getConfigValue("fov").c_str());

	double colors[4] = 
	{
		enemyRed, enemyGreen, enemyBlue, enemyAlpha,
	};

	remote::Handle csgo;

	while (true) 
	{
		if (remote::FindProcessByName("csgo_linux64", &csgo))
			break;

		usleep(500);
	}

	stringstream ss;
	ss << "\t PID:\t [";
	ss << csgo.GetPid ();
	ss << "]";

	Logger::normal (ss.str());

	remote::MapModuleMemoryRegion client;

	client.start = 0;

	while (client.start == 0) 
	{
		if (!csgo.IsRunning()) 
		{
			Logger::error("The game is not even running!");
			return 0;
		}

		csgo.ParseMaps();

		for (auto region : csgo.regions) 
		{
			if (region.filename.compare("client_client.so") == 0 && region.executable) 
			{
				client = region;
				break;
			}
		}

		usleep(500);
	}

	client.client_start = client.start;

	//unsigned long pEngine = remote::getModule("engine_client.so", csgo.GetPid());

	/*if (pEngine == 0) 
	{
		Logger::error("Couldn't find engine module!");
		return 0;
	}*/

	//csgo.a_engine_client = pEngine;

	void* foundGlowPointerCall = client.find(csgo,
		"\xE8\x00\x00\x00\x00\x48\x8b\x3d\x00\x00\x00\x00\xBE\x01\x00\x00\x00\xC7", // 2017-10-06
		"x????xxx????xxxxxx");

	unsigned long glowcalladdr = csgo.GetCallAddress(foundGlowPointerCall);

	int addressOfGlowPointerOffset;
	csgo.Read((void*) (glowcalladdr + 0x10), &addressOfGlowPointerOffset, sizeof(int));
	
	csgo.m_addressOfGlowPointer = glowcalladdr + 0x10 + addressOfGlowPointerOffset + 0x4;

	unsigned long foundLocalPlayerLea = (long)client.find(csgo,
		"\x48\x89\xe5\x74\x0e\x48\x8d\x05\x00\x00\x00\x00", //27/06/16
		"xxxxxxxx????");
	
	unsigned long PostProcessInstr = (long)client.find(csgo,
		"\x80\x3D\x00\x00\x00\x00\x00\x0F\x85\x00\x00\x00\x00\x85\xC9",
		"xx????xxx????xx");
	
	unsigned long PostProcessPointer = csgo.GetAbsoluteAddress((void*)PostProcessInstr, 2, 7);
	
	unsigned long PlayerResourcesInstr = (long)client.find(csgo,
			"\x48\x8B\x05\x00\x00\x00\x00\x55\x48\x89\xE5\x48\x85\xC0\x74\x10\x48",
			"xxx????xxxxxxxxxx");
		
	csgo.PlayerResourcesPointer = csgo.GetAbsoluteAddress((void*)(PlayerResourcesInstr), 3, 7);

	csgo.m_addressOfLocalPlayer = csgo.GetCallAddress((void*)(foundLocalPlayerLea+0x7));

	if(dumpOffsets)
	{
		Logger::address ("client_client.so:\t", client.start);
		//Logger::address ("engine_client.so:\t", pEngine);

		Logger::address ("Glow pointer:\t", addressOfGlowPointerOffset);
		Logger::address ("GlowObject pointer:\t", csgo.m_addressOfGlowPointer);

		Logger::address ("LocalPlayer function:\t", foundLocalPlayerLea);
		Logger::address ("LocalPlayer address:\t", csgo.m_addressOfLocalPlayer);

		Logger::address ("DT_CSPlayerResource pointer:\t", csgo.PlayerResourcesPointer);
	}

	csgo.GlowEnabled = false;
	csgo.RCSEnabled = false;
	csgo.TriggerEnabled = false;
	csgo.FovChangerEnabled = iFovEnabled;
	csgo.MusicKitChangerEnabled = musicKitEnabled;

	cout << endl;
	cout << CYAN << "aquaExternal for CS:GO initialized." << endl;
	cout << CYAN << " > maintained by hi im spacebar" << endl;
	cout << CYAN << " > original authors: McSwaggens and s0beit" << endl;
	cout << endl;

	char keys[32];
	char lastkeys[32];

	while (csgo.IsRunning()) 
	{
		XQueryKeymap(display, keys);

		for (unsigned i = 0; i < sizeof(keys); ++i) 
		{
			if (keys[i] != lastkeys[i]) 
			{
				for (unsigned j = 0, test = 1; j < 8; ++j, test *= 2) 
				{
					if ((keys[i] & test) && ((keys[i] & test) != (lastkeys[i] & test))) 
					{
						const int code = i * 8 + j;

						if (code == keycodeGlow) 
						{
							csgo.GlowEnabled = !csgo.GlowEnabled;
							Logger::toggle("Glow ESP\t\t", csgo.GlowEnabled);
						}
						
						if (code == keycodeRCS) 
						{
							csgo.RCSEnabled = !csgo.RCSEnabled;
							Logger::toggle("RCS\t\t", csgo.RCSEnabled);
						}
						
						if (code == keycodeTrigger) 
						{
							csgo.TriggerEnabled = !csgo.TriggerEnabled;
							Logger::toggle("Trigger\t\t", csgo.TriggerEnabled);
						} 
					}
				}
			}

			lastkeys[i] = keys[i];
		}

		bool postProcessOrig;
		csgo.Read((void*) (PostProcessPointer), &postProcessOrig, sizeof(bool));
	
		if(postProcessOrig != disablePostProcessing)
		{
			if(disablePostProcessing == 0 || disablePostProcessing == 1) // prevent writes under 0 or over 1
				csgo.Write((void*) (PostProcessPointer), &disablePostProcessing, sizeof(bool));
		}

		try
			cheat::GlowAndTrigger(colors, fullBloom, healthBased, rainbowOn, visCheck, &csgo, &client);

		catch (int exception) 
		{
			Logger::error("Couldn't find glow address, did you close the game?");
			break;
		}
		
		cheat::RCS(sensitivity, m_yaw, m_pitch, &csgo, &client);

		//cheat::SpoofMusicKit(musicKitID, &csgo, &client); // Only works on local servers because I can't get LocalPlayerIndex without the engine pointer
		
		cheat::FovChanger(iFov, &csgo, &client);
		
		std::this_thread::sleep_for(chrono::milliseconds(1)); // optimization
	}

	return 0;
}