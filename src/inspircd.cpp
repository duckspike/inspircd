/* ---------------------------------------------------------------------
 * 
 *	      +------------------------------------+
 *	      | Inspire Internet Relay Chat Daemon |
 *	      +------------------------------------+
 *
 *	 InspIRCd is copyright (C) 2002-2006 ChatSpike-Dev.
 *			     E-mail:
 *		      <brain@chatspike.net>
 *		      <Craig@chatspike.net>
 *     
 *  Written by Craig Edwards, Craig McLure, and others.
 *  This program is free but copyrighted software; you can redistribute
 *  it and/or modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation, version 2
 *  (two) ONLY.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * ---------------------------------------------------------------------
 */

#include <algorithm>
#include "inspircd_config.h"
#include "inspircd.h"
#include "configreader.h"
#include <fcntl.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <time.h>
#include <string>
#include <exception>
#include <stdexcept>
#include <new>
#include <map>
#include <sstream>
#include <fstream>
#include <vector>
#include <deque>
#include "users.h"
#include "ctables.h"
#include "globals.h"
#include "modules.h"
#include "dynamic.h"
#include "wildcard.h"
#include "mode.h"
#include "commands.h"
#include "xline.h"
#include "inspstring.h"
#include "helperfuncs.h"
#include "hashcomp.h"
#include "socketengine.h"
#include "inspircd_se_config.h"
#include "userprocess.h"
#include "socket.h"
#include "typedefs.h"
#include "command_parse.h"

using irc::sockets::BindPorts;
using irc::sockets::NonBlocking;
using irc::sockets::insp_ntoa;
using irc::sockets::insp_inaddr;
using irc::sockets::insp_sockaddr;

InspIRCd* ServerInstance = NULL;

int iterations = 0;

insp_sockaddr client, server;
socklen_t length;

time_t TIME = time(NULL), OLDTIME = time(NULL);

Server* MyServer = new Server;
char lowermap[255];

void InspIRCd::AddServerName(const std::string &servername)
{
	log(DEBUG,"Adding server name: %s",servername.c_str());
	
	if(find(servernames.begin(), servernames.end(), servername) == servernames.end())
		servernames.push_back(servername); /* Wasn't already there. */
}

const char* InspIRCd::FindServerNamePtr(const std::string &servername)
{
	servernamelist::iterator iter = find(servernames.begin(), servernames.end(), servername);
	
	if(iter == servernames.end())
	{		
		AddServerName(servername);
		iter = --servernames.end();
	}

	return iter->c_str();
}

bool InspIRCd::FindServerName(const std::string &servername)
{
	return (find(servernames.begin(), servernames.end(), servername) != servernames.end());
}

void InspIRCd::Exit(int status)
{
	if (ServerInstance->Config->log_file)
		fclose(ServerInstance->Config->log_file);
	ServerInstance->SendError("Server shutdown.");
	exit (status);
}

void InspIRCd::Start()
{
	printf("\033[1;32mInspire Internet Relay Chat Server, compiled %s at %s\n",__DATE__,__TIME__);
	printf("(C) ChatSpike Development team.\033[0m\n\n");
	printf("Developers:\t\t\033[1;32mBrain, FrostyCoolSlug, w00t, Om, Special\033[0m\n");
	printf("Others:\t\t\t\033[1;32mSee /INFO Output\033[0m\n");
	printf("Name concept:\t\t\033[1;32mLord_Zathras\033[0m\n\n");
}

void InspIRCd::Rehash(int status)
{
	ServerInstance->WriteOpers("Rehashing config file %s due to SIGHUP",ServerConfig::CleanFilename(CONFIG_FILE));
	fclose(ServerInstance->Config->log_file);
	ServerInstance->OpenLog(NULL,0);
	ServerInstance->Config->Read(false,NULL);
	FOREACH_MOD(I_OnRehash,OnRehash(""));
}

void InspIRCd::SetSignals(bool SEGVHandler)
{
	signal (SIGALRM, SIG_IGN);
	signal (SIGHUP, InspIRCd::Rehash);
	signal (SIGPIPE, SIG_IGN);
	signal (SIGTERM, InspIRCd::Exit);
	if (SEGVHandler)
		signal (SIGSEGV, InspIRCd::Error);
}

bool InspIRCd::DaemonSeed()
{
	int childpid;
	if ((childpid = fork ()) < 0)
		return (ERROR);
	else if (childpid > 0)
	{
		/* We wait a few seconds here, so that the shell prompt doesnt come back over the output */
		sleep(6);
		exit (0);
	}
	setsid ();
	umask (007);
	printf("InspIRCd Process ID: \033[1;32m%lu\033[0m\n",(unsigned long)getpid());

	rlimit rl;
	if (getrlimit(RLIMIT_CORE, &rl) == -1)
	{
		log(DEFAULT,"Failed to getrlimit()!");
		return false;
	}
	else
	{
		rl.rlim_cur = rl.rlim_max;
		if (setrlimit(RLIMIT_CORE, &rl) == -1)
			log(DEFAULT,"setrlimit() failed, cannot increase coredump size.");
	}
  
	return true;
}

void InspIRCd::WritePID(const std::string &filename)
{
	std::ofstream outfile(filename.c_str());
	if (outfile.is_open())
	{
		outfile << getpid();
		outfile.close();
	}
	else
	{
		printf("Failed to write PID-file '%s', exiting.\n",filename.c_str());
		log(DEFAULT,"Failed to write PID-file '%s', exiting.",filename.c_str());
		Exit(0);
	}
}

std::string InspIRCd::GetRevision()
{
	return REVISION;
}

void InspIRCd::MakeLowerMap()
{
	// initialize the lowercase mapping table
	for (unsigned int cn = 0; cn < 256; cn++)
		lowermap[cn] = cn;
	// lowercase the uppercase chars
	for (unsigned int cn = 65; cn < 91; cn++)
		lowermap[cn] = tolower(cn);
	// now replace the specific chars for scandanavian comparison
	lowermap[(unsigned)'['] = '{';
	lowermap[(unsigned)']'] = '}';
	lowermap[(unsigned)'\\'] = '|';
}

InspIRCd::InspIRCd(int argc, char** argv) : ModCount(-1)
{
	bool SEGVHandler = false;
	ServerInstance = this;

	modules.resize(255);
	factory.resize(255);

	this->Config = new ServerConfig(this);
	this->Start();
	this->module_sockets.clear();
	this->startup_time = time(NULL);
	srand(time(NULL));
	log(DEBUG,"*** InspIRCd starting up!");
	if (!ServerConfig::FileExists(CONFIG_FILE))
	{
		printf("ERROR: Cannot open config file: %s\nExiting...\n",CONFIG_FILE);
		log(DEFAULT,"main: no config");
		printf("ERROR: Your config file is missing, this IRCd will self destruct in 10 seconds!\n");
		Exit(ERROR);
	}
	*this->LogFileName = 0;
	if (argc > 1) {
		for (int i = 1; i < argc; i++)
		{
			if (!strcmp(argv[i],"-nofork"))
			{
				Config->nofork = true;
			}
			else if(!strcmp(argv[i],"-debug"))
			{
				Config->forcedebug = true;
			}
			else if(!strcmp(argv[i],"-nolog"))
			{
				Config->writelog = false;
			}
			else if (!strcmp(argv[i],"-wait"))
			{
				sleep(6);
			}
			else if (!strcmp(argv[i],"-nolimit"))
			{
				printf("WARNING: The `-nolimit' option is deprecated, and now on by default. This behaviour may change in the future.\n");
			}
			else if (!strcmp(argv[i],"-notraceback"))
			{
				SEGVHandler = false;
			}
			else if (!strcmp(argv[i],"-logfile"))
			{
				if (argc > i+1)
				{
					strlcpy(LogFileName,argv[i+1],MAXBUF);
					printf("LOG: Setting logfile to %s\n",LogFileName);
				}
				else
				{
					printf("ERROR: The -logfile parameter must be followed by a log file name and path.\n");
					Exit(ERROR);
				}
				i++;
			}
			else
			{
				printf("Usage: %s [-nofork] [-nolog] [-debug] [-wait] [-nolimit] [-notraceback] [-logfile <filename>]\n",argv[0]);
				Exit(ERROR);
			}
		}
	}

	strlcpy(Config->MyExecutable,argv[0],MAXBUF);

	this->MakeLowerMap();

	OpenLog(argv, argc);
	this->stats = new serverstats();
	this->Parser = new CommandParser();
	this->Timers = new TimerManager();
	Config->ClearStack();
	Config->Read(true, NULL);
	CheckRoot();
	this->ModeGrok = new ModeParser();
	this->AddServerName(Config->ServerName);
	CheckDie();
	InitializeDisabledCommands(Config->DisabledCommands, this);
	stats->BoundPortCount = BindPorts(true);

	for(int t = 0; t < 255; t++)
		Config->global_implementation[t] = 0;

	memset(&Config->implement_lists,0,sizeof(Config->implement_lists));

	printf("\n");
	this->SetSignals(SEGVHandler);
	if (!Config->nofork)
	{
		if (!this->DaemonSeed())
		{
			printf("ERROR: could not go into daemon mode. Shutting down.\n");
			Exit(ERROR);
		}
	}

	/* Because of limitations in kqueue on freebsd, we must fork BEFORE we
	 * initialize the socket engine.
	 */
	SocketEngineFactory* SEF = new SocketEngineFactory();
	SE = SEF->Create();
	delete SEF;

	/* We must load the modules AFTER initializing the socket engine, now */

	return;
}

std::string InspIRCd::GetVersionString()
{
	char versiondata[MAXBUF];
	char dnsengine[] = "singlethread-object";
	if (*Config->CustomVersion)
	{
		snprintf(versiondata,MAXBUF,"%s %s :%s",VERSION,Config->ServerName,Config->CustomVersion);
	}
	else
	{
		snprintf(versiondata,MAXBUF,"%s %s :%s [FLAGS=%lu,%s,%s]",VERSION,Config->ServerName,SYSTEM,(unsigned long)OPTIMISATION,SE->GetName().c_str(),dnsengine);
	}
	return versiondata;
}

char* InspIRCd::ModuleError()
{
	return MODERR;
}

void InspIRCd::EraseFactory(int j)
{
	int v = 0;
	for (std::vector<ircd_module*>::iterator t = factory.begin(); t != factory.end(); t++)
	{
		if (v == j)
		{
			factory.erase(t);
		 	factory.push_back(NULL);
		 	return;
	   	}
		v++;
	}
}

void InspIRCd::EraseModule(int j)
{
	int v1 = 0;
	for (ModuleList::iterator m = modules.begin(); m!= modules.end(); m++)
	{
		if (v1 == j)
		{
			DELETE(*m);
			modules.erase(m);
			modules.push_back(NULL);
			break;
		}
		v1++;
	}
	int v2 = 0;
	for (std::vector<std::string>::iterator v = Config->module_names.begin(); v != Config->module_names.end(); v++)
	{
		if (v2 == j)
		{
		       Config->module_names.erase(v);
		       break;
		}
		v2++;
	}

}

void InspIRCd::MoveTo(std::string modulename,int slot)
{
	unsigned int v2 = 256;
	for (unsigned int v = 0; v < Config->module_names.size(); v++)
	{
		if (Config->module_names[v] == modulename)
		{
			// found an instance, swap it with the item at the end
			v2 = v;
			break;
		}
	}
	if ((v2 != (unsigned int)slot) && (v2 < 256))
	{
		// Swap the module names over
		Config->module_names[v2] = Config->module_names[slot];
		Config->module_names[slot] = modulename;
		// now swap the module factories
		ircd_module* temp = factory[v2];
		factory[v2] = factory[slot];
		factory[slot] = temp;
		// now swap the module objects
		Module* temp_module = modules[v2];
		modules[v2] = modules[slot];
		modules[slot] = temp_module;
		// now swap the implement lists (we dont
		// need to swap the global or recount it)
		for (int n = 0; n < 255; n++)
		{
			char x = Config->implement_lists[v2][n];
			Config->implement_lists[v2][n] = Config->implement_lists[slot][n];
			Config->implement_lists[slot][n] = x;
		}
	}
	else
	{
		log(DEBUG,"Move of %s to slot failed!",modulename.c_str());
	}
}

void InspIRCd::MoveAfter(std::string modulename, std::string after)
{
	for (unsigned int v = 0; v < Config->module_names.size(); v++)
	{
		if (Config->module_names[v] == after)
		{
			MoveTo(modulename, v);
			return;
		}
	}
}

void InspIRCd::MoveBefore(std::string modulename, std::string before)
{
	for (unsigned int v = 0; v < Config->module_names.size(); v++)
	{
		if (Config->module_names[v] == before)
		{
			if (v > 0)
			{
				MoveTo(modulename, v-1);
			}
			else
			{
				MoveTo(modulename, v);
			}
			return;
		}
	}
}

void InspIRCd::MoveToFirst(std::string modulename)
{
	MoveTo(modulename,0);
}

void InspIRCd::MoveToLast(std::string modulename)
{
	MoveTo(modulename,this->GetModuleCount());
}

void InspIRCd::BuildISupport()
{
	// the neatest way to construct the initial 005 numeric, considering the number of configure constants to go in it...
	std::stringstream v;
	v << "WALLCHOPS WALLVOICES MODES=" << MAXMODES << " CHANTYPES=# PREFIX=(ohv)@%+ MAP MAXCHANNELS=" << MAXCHANS << " MAXBANS=60 VBANLIST NICKLEN=" << NICKMAX-1;
	v << " CASEMAPPING=rfc1459 STATUSMSG=@%+ CHARSET=ascii TOPICLEN=" << MAXTOPIC << " KICKLEN=" << MAXKICK << " MAXTARGETS=" << Config->MaxTargets << " AWAYLEN=";
	v << MAXAWAY << " CHANMODES=b,k,l,psmnti FNC NETWORK=" << Config->Network << " MAXPARA=32";
	Config->data005 = v.str();
	FOREACH_MOD(I_On005Numeric,On005Numeric(Config->data005));
}

bool InspIRCd::UnloadModule(const char* filename)
{
	std::string filename_str = filename;
	for (unsigned int j = 0; j != Config->module_names.size(); j++)
	{
		if (Config->module_names[j] == filename_str)
		{
			if (modules[j]->GetVersion().Flags & VF_STATIC)
			{
				log(DEFAULT,"Failed to unload STATIC module %s",filename);
				snprintf(MODERR,MAXBUF,"Module not unloadable (marked static)");
				return false;
			}
			/* Give the module a chance to tidy out all its metadata */
			for (chan_hash::iterator c = this->chanlist.begin(); c != this->chanlist.end(); c++)
			{
				modules[j]->OnCleanup(TYPE_CHANNEL,c->second);
			}
			for (user_hash::iterator u = this->clientlist.begin(); u != this->clientlist.end(); u++)
			{
				modules[j]->OnCleanup(TYPE_USER,u->second);
			}

			FOREACH_MOD(I_OnUnloadModule,OnUnloadModule(modules[j],Config->module_names[j]));

			for(int t = 0; t < 255; t++)
			{
				Config->global_implementation[t] -= Config->implement_lists[j][t];
			}

			/* We have to renumber implement_lists after unload because the module numbers change!
			 */
			for(int j2 = j; j2 < 254; j2++)
			{
				for(int t = 0; t < 255; t++)
				{
					Config->implement_lists[j2][t] = Config->implement_lists[j2+1][t];
				}
			}

			// found the module
			log(DEBUG,"Removing dependent commands...");
			Parser->RemoveCommands(filename);
			log(DEBUG,"Deleting module...");
			this->EraseModule(j);
			log(DEBUG,"Erasing module entry...");
			this->EraseFactory(j);
			log(DEFAULT,"Module %s unloaded",filename);
			this->ModCount--;
			BuildISupport();
			return true;
		}
	}
	log(DEFAULT,"Module %s is not loaded, cannot unload it!",filename);
	snprintf(MODERR,MAXBUF,"Module not loaded");
	return false;
}

bool InspIRCd::LoadModule(const char* filename)
{
	char modfile[MAXBUF];
#ifdef STATIC_LINK
	strlcpy(modfile,filename,MAXBUF);
#else
	snprintf(modfile,MAXBUF,"%s/%s",Config->ModPath,filename);
#endif
	std::string filename_str = filename;
#ifndef STATIC_LINK
#ifndef IS_CYGWIN
	if (!ServerConfig::DirValid(modfile))
	{
		log(DEFAULT,"Module %s is not within the modules directory.",modfile);
		snprintf(MODERR,MAXBUF,"Module %s is not within the modules directory.",modfile);
		return false;
	}
#endif
#endif
	log(DEBUG,"Loading module: %s",modfile);
#ifndef STATIC_LINK
	if (ServerConfig::FileExists(modfile))
	{
#endif
		for (unsigned int j = 0; j < Config->module_names.size(); j++)
		{
			if (Config->module_names[j] == filename_str)
			{
				log(DEFAULT,"Module %s is already loaded, cannot load a module twice!",modfile);
				snprintf(MODERR,MAXBUF,"Module already loaded");
				return false;
			}
		}
		try
		{
			ircd_module* a = new ircd_module(modfile);
			factory[this->ModCount+1] = a;
			if (factory[this->ModCount+1]->LastError())
			{
				log(DEFAULT,"Unable to load %s: %s",modfile,factory[this->ModCount+1]->LastError());
				snprintf(MODERR,MAXBUF,"Loader/Linker error: %s",factory[this->ModCount+1]->LastError());
				return false;
			}
			if ((long)factory[this->ModCount+1]->factory != -1)
			{
				Module* m = factory[this->ModCount+1]->factory->CreateModule(MyServer);
				modules[this->ModCount+1] = m;
				/* save the module and the module's classfactory, if
				 * this isnt done, random crashes can occur :/ */
				Config->module_names.push_back(filename);

				char* x = &Config->implement_lists[this->ModCount+1][0];
				for(int t = 0; t < 255; t++)
					x[t] = 0;

				modules[this->ModCount+1]->Implements(x);

				for(int t = 0; t < 255; t++)
					Config->global_implementation[t] += Config->implement_lists[this->ModCount+1][t];
			}
			else
			{
       				log(DEFAULT,"Unable to load %s",modfile);
				snprintf(MODERR,MAXBUF,"Factory function failed: Probably missing init_module() entrypoint.");
				return false;
			}
		}
		catch (ModuleException& modexcept)
		{
			log(DEFAULT,"Unable to load %s: ",modfile,modexcept.GetReason());
			snprintf(MODERR,MAXBUF,"Factory function threw an exception: %s",modexcept.GetReason());
			return false;
		}
#ifndef STATIC_LINK
	}
	else
	{
		log(DEFAULT,"InspIRCd: startup: Module Not Found %s",modfile);
		snprintf(MODERR,MAXBUF,"Module file could not be found");
		return false;
	}
#endif
	this->ModCount++;
	FOREACH_MOD(I_OnLoadModule,OnLoadModule(modules[this->ModCount],filename_str));
	// now work out which modules, if any, want to move to the back of the queue,
	// and if they do, move them there.
	std::vector<std::string> put_to_back;
	std::vector<std::string> put_to_front;
	std::map<std::string,std::string> put_before;
	std::map<std::string,std::string> put_after;
	for (unsigned int j = 0; j < Config->module_names.size(); j++)
	{
		if (modules[j]->Prioritize() == PRIORITY_LAST)
		{
			put_to_back.push_back(Config->module_names[j]);
		}
		else if (modules[j]->Prioritize() == PRIORITY_FIRST)
		{
			put_to_front.push_back(Config->module_names[j]);
		}
		else if ((modules[j]->Prioritize() & 0xFF) == PRIORITY_BEFORE)
		{
			put_before[Config->module_names[j]] = Config->module_names[modules[j]->Prioritize() >> 8];
		}
		else if ((modules[j]->Prioritize() & 0xFF) == PRIORITY_AFTER)
		{
			put_after[Config->module_names[j]] = Config->module_names[modules[j]->Prioritize() >> 8];
		}
	}
	for (unsigned int j = 0; j < put_to_back.size(); j++)
	{
		MoveToLast(put_to_back[j]);
	}
	for (unsigned int j = 0; j < put_to_front.size(); j++)
	{
		MoveToFirst(put_to_front[j]);
	}
	for (std::map<std::string,std::string>::iterator j = put_before.begin(); j != put_before.end(); j++)
	{
		MoveBefore(j->first,j->second);
	}
	for (std::map<std::string,std::string>::iterator j = put_after.begin(); j != put_after.end(); j++)
	{
		MoveAfter(j->first,j->second);
	}
	BuildISupport();
	return true;
}

void InspIRCd::DoOneIteration(bool process_module_sockets)
{
	int activefds[MAX_DESCRIPTORS];
	int incomingSockfd;
	int in_port;
	userrec* cu = NULL;
	InspSocket* s = NULL;
	InspSocket* s_del = NULL;
	unsigned int numberactive;
	insp_sockaddr sock_us;     // our port number
	socklen_t uslen;	 // length of our port number

	/* time() seems to be a pretty expensive syscall, so avoid calling it too much.
	 * Once per loop iteration is pleanty.
	 */
	OLDTIME = TIME;
	TIME = time(NULL);
	
	/* Run background module timers every few seconds
	 * (the docs say modules shouldnt rely on accurate
	 * timing using this event, so we dont have to
	 * time this exactly).
	 */
	if (((TIME % 5) == 0) && (!expire_run))
	{
		expire_lines();
		if (process_module_sockets)
		{
			FOREACH_MOD(I_OnBackgroundTimer,OnBackgroundTimer(TIME));
		}
		Timers->TickMissedTimers(TIME);
		expire_run = true;
		return;
	}   
	else if ((TIME % 5) == 1)
	{
		expire_run = false;
	}

	if (iterations++ == 15)
	{
		iterations = 0;
		this->DoBackgroundUserStuff(TIME);
	}
 
	/* Once a second, do the background processing */
	if (TIME != OLDTIME)
	{
		if (TIME < OLDTIME)
			WriteOpers("*** \002EH?!\002 -- Time is flowing BACKWARDS in this dimension! Clock drifted backwards %d secs.",abs(OLDTIME-TIME));
		if ((TIME % 3600) == 0)
		{
			irc::whowas::MaintainWhoWas(TIME);
		}
	}

	/* Process timeouts on module sockets each time around
	 * the loop. There shouldnt be many module sockets, at
	 * most, 20 or so, so this won't be much of a performance
	 * hit at all.   
	 */ 
	if (process_module_sockets)
		this->DoSocketTimeouts(TIME);
	 
	Timers->TickTimers(TIME);
	 
	/* Call the socket engine to wait on the active
	 * file descriptors. The socket engine has everything's
	 * descriptors in its list... dns, modules, users,
	 * servers... so its nice and easy, just one call.
	 */
	if (!(numberactive = SE->Wait(activefds)))
		return;

	/**
	 * Now process each of the fd's. For users, we have a fast
	 * lookup table which can find a user by file descriptor, so
	 * processing them by fd isnt expensive. If we have a lot of
	 * listening ports or module sockets though, things could get
	 * ugly.
	 */
	log(DEBUG,"There are %d fd's to process.",numberactive);

	for (unsigned int activefd = 0; activefd < numberactive; activefd++)
	{
		int socket_type = SE->GetType(activefds[activefd]);
		switch (socket_type)
		{
			case X_ESTAB_CLIENT:

				log(DEBUG,"Type: X_ESTAB_CLIENT: fd=%d",activefds[activefd]);
				cu = this->fd_ref_table[activefds[activefd]];
				if (cu)
					this->ProcessUser(cu);
	
			break;
	
			case X_ESTAB_MODULE:

				log(DEBUG,"Type: X_ESTAB_MODULE: fd=%d",activefds[activefd]);

				if (!process_module_sockets)
					break;

				/* Process module-owned sockets.
				 * Modules are encouraged to inherit their sockets from
				 * InspSocket so we can process them neatly like this.
				 */
				s = this->socket_ref[activefds[activefd]]; 
	      
				if ((s) && (!s->Poll()))
				{
					log(DEBUG,"Socket poll returned false, close and bail");
					SE->DelFd(s->GetFd());
					this->socket_ref[activefds[activefd]] = NULL;
					for (std::vector<InspSocket*>::iterator a = module_sockets.begin(); a < module_sockets.end(); a++)
					{
						s_del = *a;
						if ((s_del) && (s_del->GetFd() == activefds[activefd]))
						{
							module_sockets.erase(a);
							break;
						}
					}
					s->Close();
					DELETE(s);
				}
				else if (!s)
				{
					log(DEBUG,"WTF, X_ESTAB_MODULE for nonexistent InspSocket, removed!");
					SE->DelFd(s->GetFd());
				}
			break;

			case X_ESTAB_DNS:
				/* Handles instances of the Resolver class,
				 * a simple class extended by modules and the core for
				 * nonblocking resolving of addresses.
				 */
				this->Res->MarshallReads(activefds[activefd]);
			break;

			case X_LISTEN:

				log(DEBUG,"Type: X_LISTEN: fd=%d",activefds[activefd]);

				/* It's a listener */
				uslen = sizeof(sock_us);
				length = sizeof(client);
				incomingSockfd = accept (activefds[activefd],(struct sockaddr*)&client,&length);
	
				if ((incomingSockfd > -1) && (!getsockname(incomingSockfd,(sockaddr*)&sock_us,&uslen)))
				{
#ifdef IPV6
					in_port = ntohs(sock_us.sin6_port);
#else
					in_port = ntohs(sock_us.sin_port);
#endif
					log(DEBUG,"Accepted socket %d",incomingSockfd);
					/* Years and years ago, we used to resolve here
					 * using gethostbyaddr(). That is sucky and we
					 * don't do that any more...
					 */
					NonBlocking(incomingSockfd);
					if (Config->GetIOHook(in_port))
					{
						try
						{
#ifdef IPV6
							Config->GetIOHook(in_port)->OnRawSocketAccept(incomingSockfd, insp_ntoa(client.sin6_addr), in_port);
#else
							Config->GetIOHook(in_port)->OnRawSocketAccept(incomingSockfd, insp_ntoa(client.sin_addr), in_port);
#endif
						}
						catch (ModuleException& modexcept)
						{
							log(DEBUG,"Module exception cought: %s",modexcept.GetReason());
						}
					}
					stats->statsAccept++;
#ifdef IPV6
					log(DEBUG,"Add ipv6 client");
					userrec::AddClient(this, incomingSockfd, in_port, false, client.sin6_addr);
#else
					log(DEBUG,"Add ipv4 client");
					userrec::AddClient(this, incomingSockfd, in_port, false, client.sin_addr);
#endif
					log(DEBUG,"Adding client on port %d fd=%d",in_port,incomingSockfd);
				}
				else
				{
					log(DEBUG,"Accept failed on fd %d: %s",incomingSockfd,strerror(errno));
					shutdown(incomingSockfd,2);
					close(incomingSockfd);
					stats->statsRefused++;
				}
			break;

			default:
				/* Something went wrong if we're in here.
				 * In fact, so wrong, im not quite sure
				 * what we would do, so for now, its going
				 * to safely do bugger all.
				 */
				log(DEBUG,"Type: X_WHAT_THE_FUCK_BBQ: fd=%d",activefds[activefd]);
				SE->DelFd(activefds[activefd]);
			break;
		}
	}
}

bool InspIRCd::IsIdent(const char* n)
{
	if (!n || !*n)
		return false;

	for (char* i = (char*)n; *i; i++)
	{
		if ((*i >= 'A') && (*i <= '}'))
		{
			continue;
		}
		if (((*i >= '0') && (*i <= '9')) || (*i == '-') || (*i == '.'))
		{
			continue;
		}
		return false;
	}
	return true;
}


bool InspIRCd::IsNick(const char* n)
{
	if (!n || !*n)
		return false;

	int p = 0; 
	for (char* i = (char*)n; *i; i++, p++)
	{
		/* "A"-"}" can occur anywhere in a nickname */
		if ((*i >= 'A') && (*i <= '}'))
		{
			continue;
		}
		/* "0"-"9", "-" can occur anywhere BUT the first char of a nickname */
		if ((((*i >= '0') && (*i <= '9')) || (*i == '-')) && (i > n))
		{
			continue;
		}
		/* invalid character! abort */
		return false;
	}
	return (p < NICKMAX - 1);
}

int InspIRCd::Run()
{
	this->Res = new DNS(this);

	log(DEBUG,"RES: %08x",this->Res);

	this->LoadAllModules();

	/* Just in case no modules were loaded - fix for bug #101 */
	this->BuildISupport();

	if (!stats->BoundPortCount)
	{
		printf("\nI couldn't bind any ports! Are you sure you didn't start InspIRCd twice?\n");
		Exit(ERROR);
	}

	/* Add the listening sockets used for client inbound connections
	 * to the socket engine
	 */
	log(DEBUG,"%d listeners",stats->BoundPortCount);
	for (unsigned long count = 0; count < stats->BoundPortCount; count++)
	{
		log(DEBUG,"Add listener: %d",Config->openSockfd[count]);
		if (!SE->AddFd(Config->openSockfd[count],true,X_LISTEN))
		{
			printf("\nEH? Could not add listener to socketengine. You screwed up, aborting.\n");
			Exit(ERROR);
		}
	}

	if (!Config->nofork)
	{
		fclose(stdout);
		fclose(stderr);
		fclose(stdin);
	}

	printf("\nInspIRCd is now running!\n");

	this->WritePID(Config->PID);

	/* main loop, this never returns */
	expire_run = false;
	iterations = 0;

	while (true)
	{
		DoOneIteration(true);
	}
	/* This is never reached -- we hope! */
	return 0;
}

/**********************************************************************************/

/**
 * An ircd in four lines! bwahahaha. ahahahahaha. ahahah *cough*.
 */

int main(int argc, char** argv)
{
	/* This is a MatchCIDR() test suite -
	printf("Should be 0: %d\n",MatchCIDR("127.0.0.1","1.2.3.4/8"));
	printf("Should be 1: %d\n",MatchCIDR("127.0.0.1","127.0.0.0/8"));
	printf("Should be 1: %d\n",MatchCIDR("127.0.0.1","127.0.0.0/18"));
	printf("Should be 0: %d\n",MatchCIDR("3ffe::0","2fc9::0/16"));
	printf("Should be 1: %d\n",MatchCIDR("3ffe:1:3::0", "3ffe:1::0/32"));
	exit(0); */

	try
	{
		try
		{
			ServerInstance = new InspIRCd(argc, argv);
			ServerInstance->Run();
			DELETE(ServerInstance);
		}
		catch (std::bad_alloc&)
		{
			log(SPARSE,"You are out of memory! (got exception std::bad_alloc!)");
			ServerInstance->SendError("**** OUT OF MEMORY **** We're gonna need a bigger boat!");
		}
	}
	catch (...)
	{
		log(SPARSE,"Uncaught exception, aborting.");
		ServerInstance->SendError("Server terminating due to uncaught exception.");
	}
	return 0;
}

/* this returns true when all modules are satisfied that the user should be allowed onto the irc server
 * (until this returns true, a user will block in the waiting state, waiting to connect up to the
 * registration timeout maximum seconds)
 */
bool InspIRCd::AllModulesReportReady(userrec* user)
{
	if (!Config->global_implementation[I_OnCheckReady])
		return true;

	for (int i = 0; i <= this->GetModuleCount(); i++)
	{
		if (Config->implement_lists[i][I_OnCheckReady])
		{
			int res = modules[i]->OnCheckReady(user);
			if (!res)
				return false;
		}
	}
	return true;
}

int InspIRCd::GetModuleCount()
{
	return this->ModCount;
}

