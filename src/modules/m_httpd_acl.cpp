/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2018-2019 linuxdaemon <linuxdaemon.irc@gmail.com>
 *   Copyright (C) 2013, 2017-2018, 2020-2021 Sadie Powell <sadie@witchery.services>
 *   Copyright (C) 2013, 2015 Attila Molnar <attilamolnar@hush.com>
 *   Copyright (C) 2012, 2019 Robby <robby@chatbelgie.be>
 *   Copyright (C) 2009-2010 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2008, 2010 Craig Edwards <brain@inspircd.org>
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
#include "modules/httpd.h"

class HTTPACL
{
 public:
	std::string path;
	std::string username;
	std::string password;
	std::string whitelist;
	std::string blacklist;

	HTTPACL(const std::string &set_path, const std::string &set_username, const std::string &set_password,
		const std::string &set_whitelist, const std::string &set_blacklist)
		: path(set_path), username(set_username), password(set_password), whitelist(set_whitelist),
		blacklist(set_blacklist) { }
};

class ModuleHTTPAccessList : public Module, public HTTPACLEventListener
{
 private:
	std::vector<HTTPACL> acl_list;
	HTTPdAPI API;

 public:
	ModuleHTTPAccessList()
		: Module(VF_VENDOR, "Allows the server administrator to control who can access resources served over HTTP with the httpd module.")
		, HTTPACLEventListener(this)
		, API(this)
	{
	}

	void ReadConfig(ConfigStatus& status) override
	{
		std::vector<HTTPACL> new_acls;
		for (const auto& [_, c] : ServerInstance->Config->ConfTags("httpdacl"))
		{
			std::string path = c->getString("path");
			std::string types = c->getString("types");
			irc::commasepstream sep(types);
			std::string type;
			std::string username;
			std::string password;
			std::string whitelist;
			std::string blacklist;

			while (sep.GetToken(type))
			{
				if (stdalgo::string::equalsci(type, "password"))
				{
					username = c->getString("username");
					password = c->getString("password");
				}
				else if (stdalgo::string::equalsci(type, "whitelist"))
				{
					whitelist = c->getString("whitelist");
				}
				else if (stdalgo::string::equalsci(type, "blacklist"))
				{
					blacklist = c->getString("blacklist");
				}
				else
				{
					throw ModuleException("Invalid HTTP ACL type '" + type + "'");
				}
			}

			ServerInstance->Logs.Log(MODNAME, LOG_DEBUG, "Read ACL: path=%s pass=%s whitelist=%s blacklist=%s", path.c_str(),
					password.c_str(), whitelist.c_str(), blacklist.c_str());

			new_acls.emplace_back(path, username, password, whitelist, blacklist);
		}
		acl_list.swap(new_acls);
	}

	void BlockAccess(HTTPRequest* http, unsigned int returnval, const std::string &extraheaderkey = "", const std::string &extraheaderval="")
	{
		ServerInstance->Logs.Log(MODNAME, LOG_DEBUG, "BlockAccess (%u)", returnval);

		std::stringstream data;
		data << "<html><head></head><body style='font-family: sans-serif; text-align: center'>"
			<< "<h1 style='font-size: 48pt'>Error " << returnval << "</h1>"
			<< "<h2 style='font-size: 24pt'>Access to this resource is denied by an access control list.</h2>"
			<< "<h2 style='font-size: 24pt'>Please contact your IRC administrator.</h2><hr>"
			<< "<small>Powered by <a href='https://www.inspircd.org'>InspIRCd</a></small></body></html>";

		HTTPDocumentResponse response(this, *http, &data, returnval);
		response.headers.SetHeader("X-Powered-By", MODNAME);
		if (!extraheaderkey.empty())
			response.headers.SetHeader(extraheaderkey, extraheaderval);
		API->SendResponse(response);
	}

	bool IsAccessAllowed(HTTPRequest* http)
	{
		{
			ServerInstance->Logs.Log(MODNAME, LOG_DEBUG, "Handling httpd acl event");

			for (const auto& acl : acl_list)
			{
				if (InspIRCd::Match(http->GetPath(), acl.path, ascii_case_insensitive_map))
				{
					if (!acl.blacklist.empty())
					{
						/* Blacklist */
						irc::commasepstream sep(acl.blacklist);
						std::string entry;

						while (sep.GetToken(entry))
						{
							if (InspIRCd::Match(http->GetIP(), entry, ascii_case_insensitive_map))
							{
								ServerInstance->Logs.Log(MODNAME, LOG_DEBUG, "Denying access to blacklisted resource %s (matched by pattern %s) from ip %s (matched by entry %s)",
										http->GetPath().c_str(), acl.path.c_str(), http->GetIP().c_str(), entry.c_str());
								BlockAccess(http, 403);
								return false;
							}
						}
					}
					if (!acl.whitelist.empty())
					{
						/* Whitelist */
						irc::commasepstream sep(acl.whitelist);
						std::string entry;
						bool allow_access = false;

						while (sep.GetToken(entry))
						{
							if (InspIRCd::Match(http->GetIP(), entry, ascii_case_insensitive_map))
								allow_access = true;
						}

						if (!allow_access)
						{
							ServerInstance->Logs.Log(MODNAME, LOG_DEBUG, "Denying access to whitelisted resource %s (matched by pattern %s) from ip %s (Not in whitelist)",
									http->GetPath().c_str(), acl.path.c_str(), http->GetIP().c_str());
							BlockAccess(http, 403);
							return false;
						}
					}
					if (!acl.password.empty() && !acl.username.empty())
					{
						/* Password auth, first look to see if we have a basic authentication header */
						ServerInstance->Logs.Log(MODNAME, LOG_DEBUG, "Checking HTTP auth password for resource %s (matched by pattern %s) from ip %s, against username %s",
								http->GetPath().c_str(), acl.path.c_str(), http->GetIP().c_str(), acl.username.c_str());

						if (http->headers->IsSet("Authorization"))
						{
							/* Password has been given, validate it */
							std::string authorization = http->headers->GetHeader("Authorization");
							irc::spacesepstream sep(authorization);
							std::string authtype;
							std::string base64;

							sep.GetToken(authtype);
							if (authtype == "Basic")
							{
								std::string user;
								std::string pass;

								sep.GetToken(base64);
								std::string userpass = Base64::Decode(base64);
								ServerInstance->Logs.Log(MODNAME, LOG_DEBUG, "HTTP authorization: %s (%s)", userpass.c_str(), base64.c_str());

								irc::sepstream userpasspair(userpass, ':');
								if (userpasspair.GetToken(user))
								{
									userpasspair.GetToken(pass);

									/* Access granted if username and password are correct */
									if (user == acl.username && pass == acl.password)
									{
										ServerInstance->Logs.Log(MODNAME, LOG_DEBUG, "HTTP authorization: password and username match");
										return true;
									}
									else
									{
										/* Invalid password */
										ServerInstance->Logs.Log(MODNAME, LOG_DEBUG, "HTTP authorization: password and username do not match");
										BlockAccess(http, 401, "WWW-Authenticate", "Basic realm=\"Restricted Object\"");
									}
								}
								else
								{
									/* Malformed user:pass pair */
									ServerInstance->Logs.Log(MODNAME, LOG_DEBUG, "HTTP authorization: password and username malformed");
									BlockAccess(http, 401, "WWW-Authenticate", "Basic realm=\"Restricted Object\"");
								}
							}
							else
							{
								/* Unsupported authentication type */
								ServerInstance->Logs.Log(MODNAME, LOG_DEBUG, "HTTP authorization: unsupported auth type: %s", authtype.c_str());
								BlockAccess(http, 401, "WWW-Authenticate", "Basic realm=\"Restricted Object\"");
							}
						}
						else
						{
							/* No password given at all, access denied */
							ServerInstance->Logs.Log(MODNAME, LOG_DEBUG, "HTTP authorization: password and username not sent");
							BlockAccess(http, 401, "WWW-Authenticate", "Basic realm=\"Restricted Object\"");
						}
						return false;
					}

					/* A path may only match one ACL (the first it finds in the config file) */
					break;
				}
			}
		}
		return true;
	}

	ModResult OnHTTPACLCheck(HTTPRequest& req) override
	{
		if (IsAccessAllowed(&req))
			return MOD_RES_PASSTHRU;
		return MOD_RES_DENY;
	}
};

MODULE_INIT(ModuleHTTPAccessList)
