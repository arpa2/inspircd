/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2013, 2018, 2020 Sadie Powell <sadie@witchery.services>
 *   Copyright (C) 2012-2013 Attila Molnar <attilamolnar@hush.com>
 *   Copyright (C) 2012, 2019 Robby <robby@chatbelgie.be>
 *   Copyright (C) 2009 Uli Schlachter <psychon@inspircd.org>
 *   Copyright (C) 2009 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2007 Robin Burchell <robin+git@viroteck.net>
 *   Copyright (C) 2007 Dennis Friis <peavey@inspircd.org>
 *   Copyright (C) 2006, 2008, 2010 Craig Edwards <brain@inspircd.org>
 *   Copyright (C) 2006 Oliver Lupton <om@inspircd.org>
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

class CommandSetident : public Command
{
 public:
 CommandSetident(Module* Creator) : Command(Creator,"SETIDENT", 1)
	{
		allow_empty_last_param = false;
		access_needed = CmdAccess::OPERATOR;
		syntax = { "<ident>" };
	}

	CmdResult Handle(User* user, const Params& parameters) override
	{
		if (parameters[0].size() > ServerInstance->Config->Limits.MaxUser)
		{
			user->WriteNotice("*** SETIDENT: Ident is too long");
			return CmdResult::FAILURE;
		}

		if (!ServerInstance->IsIdent(parameters[0]))
		{
			user->WriteNotice("*** SETIDENT: Invalid characters in ident");
			return CmdResult::FAILURE;
		}

		user->ChangeIdent(parameters[0]);
		ServerInstance->SNO.WriteGlobalSno('a', "%s used SETIDENT to change their ident to '%s'", user->nick.c_str(), user->ident.c_str());

		return CmdResult::SUCCESS;
	}
};

class ModuleSetIdent : public Module
{
 private:
	CommandSetident cmd;

 public:
	ModuleSetIdent()
		: Module(VF_VENDOR, "Adds the /SETIDENT command which allows server operators to change their username (ident).")
		, cmd(this)
	{
	}
};

MODULE_INIT(ModuleSetIdent)
