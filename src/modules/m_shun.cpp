/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2019 Matt Schatz <genius3000@g3k.solutions>
 *   Copyright (C) 2018 linuxdaemon <linuxdaemon.irc@gmail.com>
 *   Copyright (C) 2017-2018 B00mX0r <b00mx0r@aureus.pw>
 *   Copyright (C) 2013, 2017-2018, 2020-2021 Sadie Powell <sadie@witchery.services>
 *   Copyright (C) 2012-2013, 2015-2016 Attila Molnar <attilamolnar@hush.com>
 *   Copyright (C) 2012, 2019 Robby <robby@chatbelgie.be>
 *   Copyright (C) 2012 Jens Voss <DukePyrolator@anope.org>
 *   Copyright (C) 2009 Uli Schlachter <psychon@inspircd.org>
 *   Copyright (C) 2009 Dennis Friis <peavey@inspircd.org>
 *   Copyright (C) 2009 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2008-2010 Craig Edwards <brain@inspircd.org>
 *   Copyright (C) 2008 Thomas Stagner <aquanight@inspircd.org>
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
#include "xline.h"
#include "modules/shun.h"
#include "modules/stats.h"


/** An XLineFactory specialized to generate shun pointers
 */
class ShunFactory : public XLineFactory
{
 public:
	ShunFactory() : XLineFactory("SHUN") { }

	XLine* Generate(time_t set_time, unsigned long duration, const std::string& source, const std::string& reason, const std::string& xline_specific_mask) override
	{
		return new Shun(set_time, duration, source, reason, xline_specific_mask);
	}

	bool AutoApplyToUserList(XLine* x) override
	{
		return false;
	}
};

class CommandShun : public Command
{
 public:
	CommandShun(Module* Creator) : Command(Creator, "SHUN", 1, 3)
	{
		access_needed = CmdAccess::OPERATOR;
		syntax = { "<nick!user@host> [<duration> :<reason>]" };
	}

	CmdResult Handle(User* user, const Params& parameters) override
	{
		/* syntax: SHUN nick!user@host time :reason goes here */
		/* 'time' is a human-readable timestring, like 2d3h2s. */

		std::string target = parameters[0];

		User *find = ServerInstance->Users.Find(target);
		if ((find) && (find->registered == REG_ALL))
			target = std::string("*!*@") + find->GetIPString();

		if (parameters.size() == 1)
		{
			std::string reason;

			if (ServerInstance->XLines->DelLine(parameters[0].c_str(), "SHUN", reason, user))
			{
				ServerInstance->SNO.WriteToSnoMask('x', "%s removed SHUN on %s: %s", user->nick.c_str(), parameters[0].c_str(), reason.c_str());
			}
			else if (ServerInstance->XLines->DelLine(target.c_str(), "SHUN", reason, user))
			{
				ServerInstance->SNO.WriteToSnoMask('x', "%s removed SHUN on %s: %s", user->nick.c_str(), target.c_str(), reason.c_str());
			}
			else
			{
				user->WriteNotice("*** Shun " + parameters[0] + " not found on the list.");
				return CmdResult::FAILURE;
			}
		}
		else
		{
			// Adding - XXX todo make this respect <insane> tag perhaps..
			unsigned long duration;
			std::string expr;
			if (parameters.size() > 2)
			{
				if (!InspIRCd::Duration(parameters[1], duration))
				{
					user->WriteNotice("*** Invalid duration for SHUN.");
					return CmdResult::FAILURE;
				}
				expr = parameters[2];
			}
			else
			{
				duration = 0;
				expr = parameters[1];
			}

			Shun* r = new Shun(ServerInstance->Time(), duration, user->nick.c_str(), expr.c_str(), target.c_str());
			if (ServerInstance->XLines->AddLine(r, user))
			{
				if (!duration)
				{
					ServerInstance->SNO.WriteToSnoMask('x', "%s added permanent SHUN for %s: %s",
						user->nick.c_str(), target.c_str(), expr.c_str());
				}
				else
				{
					ServerInstance->SNO.WriteToSnoMask('x', "%s added timed SHUN for %s, expires in %s (on %s): %s",
						user->nick.c_str(), target.c_str(), InspIRCd::DurationString(duration).c_str(),
						InspIRCd::TimeString(ServerInstance->Time() + duration).c_str(), expr.c_str());
				}
			}
			else
			{
				delete r;
				user->WriteNotice("*** Shun for " + target + " already exists.");
				return CmdResult::FAILURE;
			}
		}
		return CmdResult::SUCCESS;
	}
};

class ModuleShun : public Module, public Stats::EventListener
{
 private:
	CommandShun cmd;
	ShunFactory shun;
	insp::flat_set<std::string, irc::insensitive_swo> cleanedcommands;
	insp::flat_set<std::string, irc::insensitive_swo> enabledcommands;
	bool allowconnect;
	bool allowtags;
	bool notifyuser;

	bool IsShunned(LocalUser* user)
	{
		// Exempt the user if they are not fully connected and allowconnect is enabled.
		if (allowconnect && user->registered != REG_ALL)
			return false;

		// Exempt the user from shuns if they are an oper with the servers/ignore-shun privilege.
		if (user->HasPrivPermission("servers/ignore-shun"))
			return false;

		// Check whether the user is actually shunned.
		return ServerInstance->XLines->MatchesLine("SHUN", user);
	}

 public:
	ModuleShun()
		: Module(VF_VENDOR | VF_COMMON, "Adds the /SHUN command which allows server operators to prevent users from executing commands.")
		, Stats::EventListener(this)
		, cmd(this)
	{
	}

	void init() override
	{
		ServerInstance->XLines->RegisterFactory(&shun);
	}

	~ModuleShun() override
	{
		ServerInstance->XLines->DelAll("SHUN");
		ServerInstance->XLines->UnregisterFactory(&shun);
	}

	void Prioritize() override
	{
		Module* alias = ServerInstance->Modules.Find("alias");
		ServerInstance->Modules.SetPriority(this, I_OnPreCommand, PRIORITY_BEFORE, alias);
	}

	ModResult OnStats(Stats::Context& stats) override
	{
		if (stats.GetSymbol() != 'H')
			return MOD_RES_PASSTHRU;

		ServerInstance->XLines->InvokeStats("SHUN", stats);
		return MOD_RES_DENY;
	}

	void ReadConfig(ConfigStatus& status) override
	{
		auto tag = ServerInstance->Config->ConfValue("shun");

		cleanedcommands.clear();
		irc::spacesepstream cleanedcmds(tag->getString("cleanedcommands", "AWAY PART QUIT"));
		for (std::string cleanedcmd; cleanedcmds.GetToken(cleanedcmd); )
			cleanedcommands.insert(cleanedcmd);

		enabledcommands.clear();
		irc::spacesepstream enabledcmds(tag->getString("enabledcommands", "ADMIN OPER PING PONG QUIT", 1));
		for (std::string enabledcmd; enabledcmds.GetToken(enabledcmd); )
			enabledcommands.insert(enabledcmd);

		allowtags = tag->getBool("allowtags");
		allowconnect = tag->getBool("allowconnect");
		notifyuser = tag->getBool("notifyuser", true);
	}

	ModResult OnPreCommand(std::string& command, CommandBase::Params& parameters, LocalUser* user, bool validated) override
	{
		if (validated || !IsShunned(user))
			return MOD_RES_PASSTHRU;

		if (!enabledcommands.count(command))
		{
			if (notifyuser)
				user->WriteNotice("*** " + command + " command not processed as you have been blocked from issuing commands.");
			return MOD_RES_DENY;
		}

		if (!allowtags)
		{
			// Remove all client tags.
			ClientProtocol::TagMap& tags = parameters.GetTags();
			for (ClientProtocol::TagMap::iterator tag = tags.begin(); tag != tags.end(); )
			{
				if (tag->first[0] == '+')
					tag = tags.erase(tag);
				else
					tag++;
			}
		}

		if (cleanedcommands.count(command))
		{
			if (command == "AWAY" && !parameters.empty())
			{
				// Allow away but only for unsetting.
				parameters.clear();
			}
			else if (command == "PART" && parameters.size() > 1)
			{
				// Allow part but strip the message.
				parameters.pop_back();
			}
			else if (command == "QUIT" && !parameters.empty())
			{
				// Allow quit but strip the message.
				parameters.clear();
			}
		}

		return MOD_RES_PASSTHRU;
	}
};

MODULE_INIT(ModuleShun)
