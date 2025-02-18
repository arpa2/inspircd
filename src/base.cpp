/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2013, 2018-2019, 2021 Sadie Powell <sadie@witchery.services>
 *   Copyright (C) 2012, 2014-2015 Attila Molnar <attilamolnar@hush.com>
 *   Copyright (C) 2012 Robby <robby@chatbelgie.be>
 *   Copyright (C) 2010 Craig Edwards <brain@inspircd.org>
 *   Copyright (C) 2009 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2007 Dennis Friis <peavey@inspircd.org>
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
#include "base.h"

// This trick detects heap allocations of refcountbase objects
static void* last_heap = NULL;

void* refcountbase::operator new(size_t size)
{
	last_heap = ::operator new(size);
	return last_heap;
}

void refcountbase::operator delete(void* obj)
{
	if (last_heap == obj)
		last_heap = NULL;
	::operator delete(obj);
}

refcountbase::refcountbase()
{
	if (this != last_heap)
		throw CoreException("Reference allocate on the stack!");
}

refcountbase::~refcountbase()
{
	if (refcount && ServerInstance)
		ServerInstance->Logs.Log("CULLLIST", LOG_DEBUG, "refcountbase::~ @%p with refcount %d",
			static_cast<void*>(this), refcount);
}

usecountbase::~usecountbase()
{
	if (usecount && ServerInstance)
		ServerInstance->Logs.Log("CULLLIST", LOG_DEBUG, "usecountbase::~ @%p with refcount %d",
			static_cast<void*>(this), usecount);
}

void ServiceProvider::RegisterService()
{
}

ModuleException::ModuleException(const std::string &message, Module* who)
	: CoreException(message, who ? who->ModuleSourceFile : "A Module")
{
}
