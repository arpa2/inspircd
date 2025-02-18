/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2013, 2018, 2020 Sadie Powell <sadie@witchery.services>
 *   Copyright (C) 2012-2014, 2016 Attila Molnar <attilamolnar@hush.com>
 *   Copyright (C) 2012, 2019 Robby <robby@chatbelgie.be>
 *   Copyright (C) 2010 Craig Edwards <brain@inspircd.org>
 *   Copyright (C) 2009 John Brooks <special@inspircd.org>
 *   Copyright (C) 2009 Daniel De Graaf <danieldg@inspircd.org>
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

class CommandSakick : public Command
{
 public:
	CommandSakick(Module* Creator) : Command(Creator,"SAKICK", 2, 3)
	{
		access_needed = CmdAccess::OPERATOR;
		syntax = { "<channel> <nick> [:<reason>]" };
		translation = { TR_TEXT, TR_NICK, TR_TEXT };
	}

	CmdResult Handle(User* user, const Params& parameters) override
	{
		User* dest = ServerInstance->Users.Find(parameters[1]);
		Channel* channel = ServerInstance->Channels.Find(parameters[0]);

		if ((dest) && (dest->registered == REG_ALL) && (channel))
		{
			const std::string& reason = (parameters.size() > 2) ? parameters[2] : dest->nick;

			if (dest->server->IsService())
			{
				user->WriteNumeric(ERR_NOPRIVILEGES, "Cannot use an SA command on a U-lined client");
				return CmdResult::FAILURE;
			}

			if (!channel->HasUser(dest))
			{
				user->WriteNotice("*** " + dest->nick + " is not on " + channel->name);
				return CmdResult::FAILURE;
			}

			/* For local clients, directly kick them. For remote clients,
			 * just return CmdResult::SUCCESS knowing the protocol module will route the SAKICK to the user's
			 * local server and that will kick them instead.
			 */
			if (IS_LOCAL(dest))
			{
				// Target is on this server, kick them and send the snotice
				channel->KickUser(ServerInstance->FakeClient, dest, reason);
				ServerInstance->SNO.WriteGlobalSno('a', user->nick + " SAKICKed " + dest->nick + " on " + channel->name);
			}

			return CmdResult::SUCCESS;
		}
		else
		{
			user->WriteNotice("*** Invalid nickname or channel");
		}

		return CmdResult::FAILURE;
	}

	RouteDescriptor GetRouting(User* user, const Params& parameters) override
	{
		return ROUTE_OPT_UCAST(parameters[1]);
	}
};

class ModuleSakick : public Module
{
 private:
	CommandSakick cmd;

 public:
	ModuleSakick()
		: Module(VF_VENDOR | VF_OPTCOMMON, "Adds the /SAKICK command which allows server operators to kick users from a channel without having any privileges in the channel.")
		, cmd(this)
	{
	}
};

MODULE_INIT(ModuleSakick)
