/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2013, 2018, 2020 Sadie Powell <sadie@witchery.services>
 *   Copyright (C) 2012-2014, 2016 Attila Molnar <attilamolnar@hush.com>
 *   Copyright (C) 2012, 2019 Robby <robby@chatbelgie.be>
 *   Copyright (C) 2009 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2007-2008 Robin Burchell <robin+git@viroteck.net>
 *   Copyright (C) 2007 Dennis Friis <peavey@inspircd.org>
 *   Copyright (C) 2004, 2007-2008, 2010 Craig Edwards <brain@inspircd.org>
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

class CommandSapart : public Command
{
 public:
	CommandSapart(Module* Creator) : Command(Creator,"SAPART", 2, 3)
	{
		access_needed = CmdAccess::OPERATOR;
		syntax = { "<nick> <channel>[,<channel>]+ [:<reason>]" };
		translation = { TR_NICK, TR_TEXT, TR_TEXT };
	}

	CmdResult Handle(User* user, const Params& parameters) override
	{
		if (CommandParser::LoopCall(user, this, parameters, 1))
			return CmdResult::FAILURE;

		User* dest = ServerInstance->Users.Find(parameters[0]);
		Channel* channel = ServerInstance->Channels.Find(parameters[1]);
		std::string reason;

		if ((dest) && (dest->registered == REG_ALL) && (channel))
		{
			if (parameters.size() > 2)
				reason = parameters[2];

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

			/* For local clients, directly part them generating a PART message. For remote clients,
			 * just return CmdResult::SUCCESS knowing the protocol module will route the SAPART to the users
			 * local server and that will generate the PART instead
			 */
			if (IS_LOCAL(dest))
			{
				channel->PartUser(dest, reason);
				ServerInstance->SNO.WriteGlobalSno('a', user->nick+" used SAPART to make "+dest->nick+" part "+channel->name);
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
		return ROUTE_OPT_UCAST(parameters[0]);
	}
};


class ModuleSapart : public Module
{
 private:
	CommandSapart cmd;

 public:
	ModuleSapart()
		: Module(VF_VENDOR | VF_OPTCOMMON, "Adds the /SAPART command which allows server operators to force part users from one or more channels without having any privileges in these channels.")
		, cmd(this)
	{
	}
};

MODULE_INIT(ModuleSapart)
