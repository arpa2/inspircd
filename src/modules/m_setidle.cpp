/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2018 linuxdaemon <linuxdaemon.irc@gmail.com>
 *   Copyright (C) 2013, 2017-2018, 2020 Sadie Powell <sadie@witchery.services>
 *   Copyright (C) 2012-2013 Attila Molnar <attilamolnar@hush.com>
 *   Copyright (C) 2012, 2019 Robby <robby@chatbelgie.be>
 *   Copyright (C) 2009 Uli Schlachter <psychon@inspircd.org>
 *   Copyright (C) 2009 Robin Burchell <robin+git@viroteck.net>
 *   Copyright (C) 2009 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2007 Dennis Friis <peavey@inspircd.org>
 *   Copyright (C) 2005, 2010 Craig Edwards <brain@inspircd.org>
 *
 * This file is part of InspIRCd.  InspIRCd is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "inspircd.h"

enum
{
	// InspIRCd-specific.
	ERR_INVALIDIDLETIME = 948,
	RPL_IDLETIMESET = 944
};

class CommandSetidle : public SplitCommand
{
 public:
	CommandSetidle(Module* Creator) : SplitCommand(Creator,"SETIDLE", 1)
	{
		access_needed = CmdAccess::OPERATOR;
		syntax = { "<duration>" };
	}

	CmdResult HandleLocal(LocalUser* user, const Params& parameters) override
	{
		unsigned long idle;
		if (!InspIRCd::Duration(parameters[0], idle))
		{
			user->WriteNumeric(ERR_INVALIDIDLETIME, "Invalid idle time.");
			return CmdResult::FAILURE;
		}
		user->idle_lastmsg = (ServerInstance->Time() - idle);
		// minor tweak - we cant have signon time shorter than our idle time!
		if (user->signon > user->idle_lastmsg)
			user->signon = user->idle_lastmsg;
		ServerInstance->SNO.WriteToSnoMask('a', user->nick+" used SETIDLE to set their idle time to "+ConvToStr(idle)+" seconds");
		user->WriteNumeric(RPL_IDLETIMESET, "Idle time set.");

		return CmdResult::SUCCESS;
	}
};


class ModuleSetIdle : public Module
{
 private:
	CommandSetidle cmd;

 public:
	ModuleSetIdle()
		: Module(VF_VENDOR, "Adds the /SETIDLE command which allows server operators to change their idle time.")
		, cmd(this)
	{
	}
};

MODULE_INIT(ModuleSetIdle)
