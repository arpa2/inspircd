/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2018 linuxdaemon <linuxdaemon.irc@gmail.com>
 *   Copyright (C) 2013-2014, 2016-2021 Sadie Powell <sadie@witchery.services>
 *   Copyright (C) 2013 ChrisTX <xpipe@hotmail.de>
 *   Copyright (C) 2012-2014 Attila Molnar <attilamolnar@hush.com>
 *   Copyright (C) 2012 Robby <robby@chatbelgie.be>
 *   Copyright (C) 2010 Craig Edwards <brain@inspircd.org>
 *   Copyright (C) 2009-2010 Daniel De Graaf <danieldg@inspircd.org>
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


#include <filesystem>
#include <fstream>

#include "inspircd.h"
#include "configparser.h"

enum ParseFlags
{
	// Executable includes are disabled.
	FLAG_NO_EXEC = 2,

	// All includes are disabled.
	FLAG_NO_INC = 4,

	// &env.FOO; is disabled.
	FLAG_NO_ENV = 8,

	// It's okay if an include doesn't exist.
	FLAG_MISSING_OKAY = 16
};

// RAII wrapper for FILE* which closes the file when it goes out of scope.
class FileWrapper
{
 private:
	// Whether this file handle should be closed with pclose.
	bool close_with_pclose;

	// The file handle which is being wrapped.
	FILE* const file;

 public:
	FileWrapper(FILE* File, bool CloseWithPClose = false)
		: close_with_pclose(CloseWithPClose)
		, file(File)
	{
	}

	// Operator which determines whether the file is open.
	operator bool() { return (file != NULL); }

	// Operator which retrieves the underlying FILE pointer.
	operator FILE*() { return file; }

	~FileWrapper()
	{
		if (!file)
			return;

		if (close_with_pclose)
			pclose(file);
		else
			fclose(file);
	}
};


struct Parser
{
	ParseStack& stack;
	int flags;
	FILE* const file;
	FilePosition current;
	FilePosition last_tag;
	std::shared_ptr<ConfigTag> tag;
	int ungot;
	std::string mandatory_tag;

	Parser(ParseStack& me, int myflags, FILE* conf, const std::string& name, const std::string& mandatorytag)
		: stack(me)
		, flags(myflags)
		, file(conf)
		, current(name, 1, 0)
		, last_tag(name, 0, 0)
		, ungot(-1)
		, mandatory_tag(mandatorytag)
	{
	}

	int next(bool eof_ok = false)
	{
		if (ungot != -1)
		{
			int ch = ungot;
			ungot = -1;
			return ch;
		}
		int ch = fgetc(file);
		if (ch == EOF && !eof_ok)
		{
			throw CoreException("Unexpected end-of-file");
		}
		else if (ch == '\n')
		{
			current.line++;
			current.column = 1;
		}
		else
		{
			current.column++;
		}
		return ch;
	}

	void unget(int ch)
	{
		if (ungot != -1)
			throw CoreException("INTERNAL ERROR: cannot unget twice");
		ungot = ch;
	}

	void comment()
	{
		while (1)
		{
			int ch = next();
			if (ch == '\n')
				return;
		}
	}

	bool wordchar(char ch)
	{
		return isalnum(ch)
			|| ch == '-'
			|| ch == '.'
			|| ch == '_';
	}

	void nextword(std::string& rv)
	{
		int ch = next();
		while (isspace(ch))
			ch = next();
		while (wordchar(ch))
		{
			rv.push_back(ch);
			ch = next();
		}
		unget(ch);
	}

	bool kv()
	{
		std::string key;
		nextword(key);
		int ch = next();
		if (ch == '>' && key.empty())
		{
			return false;
		}
		else if (ch == '#' && key.empty())
		{
			comment();
			return true;
		}
		else if (ch != '=')
		{
			throw CoreException("Invalid character " + std::string(1, ch) + " in key (" + key + ")");
		}

		std::string value;
		ch = next();
		if (ch != '"')
		{
			throw CoreException("Invalid character in value of <" + tag->name + ":" + key + ">");
		}
		while (1)
		{
			ch = next();
			if (ch == '&')
			{
				std::string varname;
				while (1)
				{
					ch = next();
					if (wordchar(ch) || (varname.empty() && ch == '#'))
						varname.push_back(ch);
					else if (ch == ';')
						break;
					else
					{
						stack.errstr << "Invalid XML entity name in value of <" + tag->name + ":" + key + ">\n"
							<< "To include an ampersand or quote, use &amp; or &quot;\n";
						throw CoreException("Parse error");
					}
				}
				if (varname.empty())
					throw CoreException("Empty XML entity reference");
				else if (varname[0] == '#' && (varname.size() == 1 || (varname.size() == 2 && varname[1] == 'x')))
					throw CoreException("Empty numeric character reference");
				else if (varname[0] == '#')
				{
					const char* cvarname = varname.c_str();
					char* endptr;
					unsigned long lvalue;
					if (cvarname[1] == 'x')
						lvalue = strtoul(cvarname + 2, &endptr, 16);
					else
						lvalue = strtoul(cvarname + 1, &endptr, 10);
					if (*endptr != '\0' || lvalue > 255)
						throw CoreException("Invalid numeric character reference '&" + varname + ";'");
					value.push_back(static_cast<char>(lvalue));
				}
				else if (varname.compare(0, 4, "env.") == 0)
				{
					if (flags & FLAG_NO_ENV)
						throw CoreException("XML environment entity reference in file included with noenv=\"yes\"");

					const char* envstr = getenv(varname.c_str() + 4);
					if (!envstr)
						throw CoreException("Undefined XML environment entity reference '&" + varname + ";'");

					value.append(envstr);
				}
				else
				{
					insp::flat_map<std::string, std::string>::iterator var = stack.vars.find(varname);
					if (var == stack.vars.end())
						throw CoreException("Undefined XML entity reference '&" + varname + ";'");
					value.append(var->second);
				}
			}
			else if (ch == '"')
				break;
			else if (ch != '\r')
				value.push_back(ch);
		}

		if (!tag->GetItems().insert({key, value}).second)
			throw CoreException("Duplicate key '" + key + "' found");
		return true;
	}

	void dotag()
	{
		last_tag = current;
		std::string name;
		nextword(name);

		int spc = next();
		if (spc == '>')
			unget(spc);
		else if (!isspace(spc))
			throw CoreException("Invalid character in tag name");

		if (name.empty())
			throw CoreException("Empty tag name");

		tag = std::make_shared<ConfigTag>(name, last_tag);
		while (kv())
		{
			// Do nothing here (silences a GCC warning).
		}

		if (name == mandatory_tag)
		{
			// Found the mandatory tag
			mandatory_tag.clear();
		}

		if (stdalgo::string::equalsci(name, "include"))
		{
			stack.DoInclude(tag, flags);
		}
		else if (stdalgo::string::equalsci(name, "files"))
		{
			for (const auto& [key, value] : tag->GetItems())
				stack.DoReadFile(key, value, flags, false);
		}
		else if (stdalgo::string::equalsci(name, "execfiles"))
		{
			for (const auto& [key, value] : tag->GetItems())
				stack.DoReadFile(key, value, flags, true);
		}
		else if (stdalgo::string::equalsci(name, "define"))
		{
			std::string varname = tag->getString("name");
			std::string value = tag->getString("value");
			if (varname.empty())
				throw CoreException("Variable definition must include variable name");
			stack.vars[varname] = value;
		}
		else
		{
			stack.output.emplace(name, tag);
		}
		// this is not a leak; shared_ptr takes care of the delete
		tag = NULL;
	}

	bool outer_parse()
	{
		try
		{
			while (1)
			{
				int ch = next(true);
				switch (ch)
				{
					case EOF:
						// this is the one place where an EOF is not an error
						if (!mandatory_tag.empty())
							throw CoreException("Mandatory tag \"" + mandatory_tag + "\" not found");
						return true;
					case '#':
						comment();
						break;
					case '<':
						dotag();
						break;
					case ' ':
					case '\r':
					case '\t':
					case '\n':
						break;
					case 0xFE:
					case 0xFF:
						stack.errstr << "Do not save your files as UTF-16 or UTF-32, use UTF-8!\n";
						[[fallthrough]];
					default:
						throw CoreException("Syntax error - start of tag expected");
				}
			}
		}
		catch (CoreException& err)
		{
			stack.errstr << err.GetReason() << " at " << current.str();
			if (tag)
				stack.errstr << " (inside tag " << tag->name << " at line " << tag->source.line << ")\n";
			else
				stack.errstr << " (last tag was on line " << last_tag.line << ")\n";
		}
		return false;
	}
};

void ParseStack::DoInclude(std::shared_ptr<ConfigTag> tag, int flags)
{
	if (flags & FLAG_NO_INC)
		throw CoreException("Invalid <include> tag in file included with noinclude=\"yes\"");

	std::string mandatorytag;
	tag->readString("mandatorytag", mandatorytag);

	std::string name;
	if (tag->readString("file", name))
	{
		if (tag->getBool("noinclude", false))
			flags |= FLAG_NO_INC;

		if (tag->getBool("noexec", false))
			flags |= FLAG_NO_EXEC;

		if (tag->getBool("noenv", false))
			flags |= FLAG_NO_ENV;

		if (tag->getBool("missingokay", false))
			flags |= FLAG_MISSING_OKAY;
		else
			flags &= ~FLAG_MISSING_OKAY;

		if (!ParseFile(ServerInstance->Config->Paths.PrependConfig(name), flags, mandatorytag))
			throw CoreException("Included");
	}
	else if (tag->readString("directory", name))
	{
		if (tag->getBool("noinclude", false))
			flags |= FLAG_NO_INC;
		if (tag->getBool("noexec", false))
			flags |= FLAG_NO_EXEC;
		if (tag->getBool("noenv", false))
			flags |= FLAG_NO_ENV;

		const std::string includedir = ServerInstance->Config->Paths.PrependConfig(name);
		try
		{
			for (const auto& entry : std::filesystem::directory_iterator(includedir))
			{
				if (!entry.is_regular_file())
					continue;

				const std::string path = entry.path().string();
				if (!InspIRCd::Match(path, "*.conf"))
					continue;

				if (!ParseFile(path, flags, mandatorytag))
					throw CoreException("Included");
			}
		}
		catch (const std::filesystem::filesystem_error& err)
		{
			throw CoreException("Unable to read directory for include " + includedir + ": " + err.what());
		}
	}

	else if (tag->readString("executable", name))
	{
		if (flags & FLAG_NO_EXEC)
			throw CoreException("Invalid <include:executable> tag in file included with noexec=\"yes\"");
		if (tag->getBool("noinclude", false))
			flags |= FLAG_NO_INC;
		if (tag->getBool("noexec", true))
			flags |= FLAG_NO_EXEC;
		if (tag->getBool("noenv", true))
			flags |= FLAG_NO_ENV;

		if (!ParseFile(name, flags, mandatorytag, true))
			throw CoreException("Included");
	}
}

void ParseStack::DoReadFile(const std::string& key, const std::string& name, int flags, bool exec)
{
	if (flags & FLAG_NO_INC)
		throw CoreException("Invalid <files> tag in file included with noinclude=\"yes\"");
	if (exec && (flags & FLAG_NO_EXEC))
		throw CoreException("Invalid <execfiles> tag in file included with noexec=\"yes\"");

	std::string path = ServerInstance->Config->Paths.PrependConfig(name);
	FileWrapper file(exec ? popen(name.c_str(), "r") : fopen(path.c_str(), "r"), exec);
	if (!file)
		throw CoreException("Could not read \"" + path + "\" for \"" + key + "\" file");

	file_cache& cache = FilesOutput[key];
	cache.clear();

	char linebuf[5120];
	while (fgets(linebuf, sizeof(linebuf), file))
	{
		size_t len = strlen(linebuf);
		if (len)
		{
			if (linebuf[len-1] == '\n')
				len--;
			cache.push_back(std::string(linebuf, len));
		}
	}
}

bool ParseStack::ParseFile(const std::string& path, int flags, const std::string& mandatory_tag, bool isexec)
{
	ServerInstance->Logs.Log("CONFIG", LOG_DEBUG, "Reading (isexec=%d) %s", isexec, path.c_str());
	if (stdalgo::isin(reading, path))
		throw CoreException((isexec ? "Executable " : "File ") + path + " is included recursively (looped inclusion)");

	/* It's not already included, add it to the list of files we've loaded */

	FileWrapper file((isexec ? popen(path.c_str(), "r") : fopen(path.c_str(), "r")), isexec);
	if (!file)
	{
		if (flags & FLAG_MISSING_OKAY)
			return true;

		throw CoreException("Could not read \"" + path + "\" for include");
	}

	reading.push_back(path);
	Parser p(*this, flags, file, path, mandatory_tag);
	bool ok = p.outer_parse();
	reading.pop_back();
	return ok;
}

bool ConfigTag::readString(const std::string& key, std::string& value, bool allow_lf) const
{
	for (const auto& [ikey, ivalue] : items)
	{
		if (!stdalgo::string::equalsci(ikey, key))
			continue;

		value = ivalue;
		if (!allow_lf && (value.find('\n') != std::string::npos))
		{
			ServerInstance->Logs.Log("CONFIG", LOG_DEFAULT, "Value of <" + name + ":" + key + "> at " + source.str() +
				" contains a linefeed, and linefeeds in this value are not permitted -- stripped to spaces.");

			for (auto& chr : value)
			{
				if (chr == '\n')
					chr = ' ';
			}
		}
		return true;
	}
	return false;
}

std::string ConfigTag::getString(const std::string& key, const std::string& def, const std::function<bool(const std::string&)>& validator) const
{
	std::string res;
	if (!readString(key, res))
		return def;

	if (!validator(res))
	{
		ServerInstance->Logs.Log("CONFIG", LOG_DEFAULT, "WARNING: The value of <%s:%s> is not valid; value set to %s.",
			name.c_str(), key.c_str(), def.c_str());
		return def;
	}
	return res;
}

std::string ConfigTag::getString(const std::string& key, const std::string& def, size_t minlen, size_t maxlen) const
{
	std::string res;
	if (!readString(key, res))
		return def;

	if (res.length() < minlen || res.length() > maxlen)
	{
		ServerInstance->Logs.Log("CONFIG", LOG_DEFAULT, "WARNING: The length of <%s:%s> is not between %ld and %ld; value set to %s.",
			name.c_str(), key.c_str(), minlen, maxlen, def.c_str());
		return def;
	}
	return res;
}

namespace
{
	/** Check for an invalid magnitude specifier. If one is found a warning is logged and the
	 * value is corrected (set to \p def).
	 * @param tag The tag name; used in the warning message.
	 * @param key The key name; used in the warning message.
	 * @param val The full value set in the config as a string.
	 * @param num The value to verify and modify if needed.
	 * @param def The default value, \p res will be set to this if \p tail does not contain a.
	 *            valid magnitude specifier.
	 * @param tail The location in the config value at which the magnifier is located.
	 */
	template <typename Numeric>
	void CheckMagnitude(const std::string& tag, const std::string& key, const std::string& val, Numeric& num, Numeric def, const char* tail)
	{
		// If this is NULL then no magnitude specifier was given.
		if (!*tail)
			return;

		switch (toupper(*tail))
		{
			case 'K':
				num *= 1024;
				return;

			case 'M':
				num *= 1024 * 1024;
				return;

			case 'G':
				num *= 1024 * 1024 * 1024;
				return;
		}

		const std::string message = "WARNING: <" + tag + ":" + key + "> value of " + val + " contains an invalid magnitude specifier '"
			+ tail + "'; value set to " + ConvToStr(def) + ".";
		ServerInstance->Logs.Log("CONFIG", LOG_DEFAULT, message);
		num = def;
	}

	/** Check for an out of range value. If the value falls outside the boundaries a warning is
	 * logged and the value is corrected (set to \p def).
	 * @param tag The tag name; used in the warning message.
	 * @param key The key name; used in the warning message.
	 * @param num The value to verify and modify if needed.
	 * @param def The default value, \p res will be set to this if (min <= res <= max) doesn't hold true.
	 * @param min Minimum accepted value for \p res.
	 * @param max Maximum accepted value for \p res.
	 */
	template <typename Numeric>
	void CheckRange(const std::string& tag, const std::string& key, Numeric& num, Numeric def, Numeric min, Numeric max)
	{
		if (num >= min && num <= max)
			return;

		const std::string message = "WARNING: <" + tag + ":" + key + "> value of " + ConvToStr(num) + " is not between "
			+ ConvToStr(min) + " and " + ConvToStr(max) + "; value set to " + ConvToStr(def) + ".";
		ServerInstance->Logs.Log("CONFIG", LOG_DEFAULT, message);
		num = def;
	}
}

long ConfigTag::getInt(const std::string& key, long def, long min, long max) const
{
	std::string result;
	if(!readString(key, result) || result.empty())
		return def;

	const char* res_cstr = result.c_str();
	char* res_tail = NULL;
	long res = strtol(res_cstr, &res_tail, 0);
	if (res_tail == res_cstr)
		return def;

	CheckMagnitude(key, key, result, res, def, res_tail);
	CheckRange(key, key, res, def, min, max);
	return res;
}

unsigned long ConfigTag::getUInt(const std::string& key, unsigned long def, unsigned long min, unsigned long max) const
{
	std::string result;
	if (!readString(key, result) || result.empty())
		return def;

	const char* res_cstr = result.c_str();
	char* res_tail = NULL;
	unsigned long res = strtoul(res_cstr, &res_tail, 0);
	if (res_tail == res_cstr)
		return def;

	CheckMagnitude(key, key, result, res, def, res_tail);
	CheckRange(key, key, res, def, min, max);
	return res;
}

unsigned long ConfigTag::getDuration(const std::string& key, unsigned long def, unsigned long min, unsigned long max) const
{
	std::string duration;
	if (!readString(key, duration) || duration.empty())
		return def;

	unsigned long ret;
	if (!InspIRCd::Duration(duration, ret))
	{
		ServerInstance->Logs.Log("CONFIG", LOG_DEFAULT, "Value of <" + name + ":" + key + "> at " + source.str() +
			" is not a duration; value set to " + ConvToStr(def) + ".");
		return def;
	}

	CheckRange(name, key, ret, def, min, max);
	return ret;
}

double ConfigTag::getFloat(const std::string& key, double def, double min, double max) const
{
	std::string result;
	if (!readString(key, result))
		return def;

	double res = strtod(result.c_str(), NULL);
	CheckRange(name, key, res, def, min, max);
	return res;
}

bool ConfigTag::getBool(const std::string& key, bool def) const
{
	std::string result;
	if(!readString(key, result) || result.empty())
		return def;

	if (stdalgo::string::equalsci(result, "yes") || stdalgo::string::equalsci(result, "true") || stdalgo::string::equalsci(result, "on"))
		return true;

	if (stdalgo::string::equalsci(result, "no") || stdalgo::string::equalsci(result, "false") || stdalgo::string::equalsci(result, "off"))
		return false;

	ServerInstance->Logs.Log("CONFIG", LOG_DEFAULT, "Value of <" + name + ":" + key + "> at " + source.str() +
		" is not valid, ignoring");
	return def;
}

ConfigTag::ConfigTag(const std::string& Name, const FilePosition& Source)
	: name(Name)
	, source(Source)
{
}

OperInfo::OperInfo(const std::string& Name)
	: name(Name)
{
}

std::string OperInfo::getConfig(const std::string& key)
{
	std::string rv;
	if (type_block)
		type_block->readString(key, rv);
	if (oper_block)
		oper_block->readString(key, rv);
	return rv;
}
