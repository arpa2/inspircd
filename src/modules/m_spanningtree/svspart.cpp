/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2018 Sadie Powell <sadie@witchery.services>
 *   Copyright (C) 2012, 2016 Attila Molnar <attilamolnar@hush.com>
 *   Copyright (C) 2012 Robby <robby@chatbelgie.be>
 *   Copyright (C) 2009-2010 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2009 Uli Schlachter <psychon@inspircd.org>
 *   Copyright (C) 2008 Robin Burchell <robin+git@viroteck.net>
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

#include "commands.h"

CmdResult CommandSVSPart::Handle(User* user, Params& parameters)
{
	User* u = ServerInstance->Users.FindUUID(parameters[0]);
	if (!u)
		return CmdResult::FAILURE;

	Channel* c = ServerInstance->Channels.Find(parameters[1]);
	if (!c)
		return CmdResult::FAILURE;

	if (IS_LOCAL(u))
	{
		std::string reason = (parameters.size() == 3) ? parameters[2] : "Services forced part";
		c->PartUser(u, reason);
	}
	return CmdResult::SUCCESS;
}

RouteDescriptor CommandSVSPart::GetRouting(User* user, const Params& parameters)
{
	return ROUTE_OPT_UCAST(parameters[0]);
}
