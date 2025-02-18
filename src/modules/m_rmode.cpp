/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2019 Robby <robby@chatbelgie.be>
 *   Copyright (C) 2013-2014, 2016 Attila Molnar <attilamolnar@hush.com>
 *   Copyright (C) 2013, 2017-2018 Sadie Powell <sadie@witchery.services>
 *   Copyright (C) 2013 Daniel Vassdal <shutter@canternet.org>
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
#include "listmode.h"

class CommandRMode : public Command
{
 public:
	CommandRMode(Module* Creator) : Command(Creator,"RMODE", 2, 3)
	{
		allow_empty_last_param = false;
		syntax = { "<channel> <mode> [<pattern>]" };
	}

	CmdResult Handle(User* user, const Params& parameters) override
	{
		ModeHandler* mh;
		Channel* chan = ServerInstance->Channels.Find(parameters[0]);
		char modeletter = parameters[1][0];

		if (chan == NULL)
		{
			user->WriteNotice("The channel " + parameters[0] + " does not exist.");
			return CmdResult::FAILURE;
		}

		mh = ServerInstance->Modes.FindMode(modeletter, MODETYPE_CHANNEL);
		if (mh == NULL || parameters[1].size() > 1)
		{
			user->WriteNotice(parameters[1] + " is not a valid channel mode.");
			return CmdResult::FAILURE;
		}

		if (chan->GetPrefixValue(user) < mh->GetLevelRequired(false))
		{
			user->WriteNotice("You do not have access to unset " + ConvToStr(modeletter) + " on " +  chan->name + ".");
			return CmdResult::FAILURE;
		}

		std::string pattern = parameters.size() > 2 ? parameters[2] : "*";
		PrefixMode* pm;
		ListModeBase* lm;
		ListModeBase::ModeList* ml;
		Modes::ChangeList changelist;

		if ((pm = mh->IsPrefixMode()))
		{
			// As user prefix modes don't have a GetList() method, let's iterate through the channel's users.
			for (const auto& [u, memb] : chan->GetUsers())
			{
				if (!InspIRCd::Match(u->nick, pattern))
					continue;

				if (memb->HasMode(pm) && !((u == user) && (pm->GetPrefixRank() > VOICE_VALUE)))
					changelist.push_remove(mh, u->nick);
			}
		}
		else if ((lm = mh->IsListModeBase()) && ((ml = lm->GetList(chan)) != NULL))
		{
			for (const auto& entry : *ml)
			{
				if (InspIRCd::Match(entry.mask, pattern))
					changelist.push_remove(mh, entry.mask);
			}
		}
		else
		{
			if (chan->IsModeSet(mh))
				changelist.push_remove(mh);
		}

		ServerInstance->Modes.Process(user, chan, NULL, changelist);
		return CmdResult::SUCCESS;
	}
};

class ModuleRMode : public Module
{
 private:
	CommandRMode cmd;

 public:
	ModuleRMode()
		: Module(VF_VENDOR, "Allows removal of channel list modes using glob patterns.")
		, cmd(this)
	{
	}
};

MODULE_INIT(ModuleRMode)
