/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2013, 2018, 2020 Sadie Powell <sadie@witchery.services>
 *   Copyright (C) 2012-2016 Attila Molnar <attilamolnar@hush.com>
 *   Copyright (C) 2012, 2019 Robby <robby@chatbelgie.be>
 *   Copyright (C) 2009 Thomas Stagner <aquanight@inspircd.org>
 *   Copyright (C) 2009 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2007-2008 Robin Burchell <robin+git@viroteck.net>
 *   Copyright (C) 2007 John Brooks <special@inspircd.org>
 *   Copyright (C) 2007 Dennis Friis <peavey@inspircd.org>
 *   Copyright (C) 2004, 2007, 2010 Craig Edwards <brain@inspircd.org>
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

class CommandSaquit : public Command
{
 public:
	CommandSaquit(Module* Creator) : Command(Creator, "SAQUIT", 2, 2)
	{
		access_needed = CmdAccess::OPERATOR;
		syntax = { "<nick> :<reason>" };
		translation = { TR_NICK, TR_TEXT };
	}

	CmdResult Handle(User* user, const Params& parameters) override
	{
		User* dest = ServerInstance->Users.Find(parameters[0]);
		if ((dest) && (dest->registered == REG_ALL))
		{
			if (dest->server->IsService())
			{
				user->WriteNumeric(ERR_NOPRIVILEGES, "Cannot use an SA command on a U-lined client");
				return CmdResult::FAILURE;
			}

			// Pass the command on, so the client's server can quit it properly.
			if (!IS_LOCAL(dest))
				return CmdResult::SUCCESS;

			ServerInstance->SNO.WriteGlobalSno('a', user->nick+" used SAQUIT to make "+dest->nick+" quit with a reason of "+parameters[1]);

			ServerInstance->Users.QuitUser(dest, parameters[1]);
			return CmdResult::SUCCESS;
		}
		else
		{
			user->WriteNotice("*** Invalid nickname: '" + parameters[0] + "'");
			return CmdResult::FAILURE;
		}
	}

	RouteDescriptor GetRouting(User* user, const Params& parameters) override
	{
		return ROUTE_OPT_UCAST(parameters[0]);
	}
};

class ModuleSaquit : public Module
{
 private:
	CommandSaquit cmd;

 public:
	ModuleSaquit()
		: Module(VF_VENDOR | VF_OPTCOMMON, "Adds the /SAQUIT command which allows server operators to disconnect users from the server.")
		, cmd(this)
	{
	}
};

MODULE_INIT(ModuleSaquit)
