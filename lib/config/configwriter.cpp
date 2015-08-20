/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012-2015 Icinga Development Team (http://www.icinga.org)    *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#include "config/configwriter.hpp"
#include "config/configcompiler.hpp"
#include "base/exception.hpp"
#include <boost/foreach.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <set>
#include <iterator>

using namespace icinga;

void ConfigWriter::EmitBoolean(std::ostream& fp, bool val)
{
	fp << (val ? "true" : "false");
}

void ConfigWriter::EmitNumber(std::ostream& fp, double val)
{
	fp << val;
}

void ConfigWriter::EmitString(std::ostream& fp, const String& val)
{
	fp << "\"" << EscapeIcingaString(val) << "\"";
}

void ConfigWriter::EmitEmpty(std::ostream& fp)
{
	fp << "null";
}

void ConfigWriter::EmitArray(std::ostream& fp, const Array::Ptr& val)
{
	fp << "[ ";
	EmitArrayItems(fp, val);
	fp << " ]";
}

void ConfigWriter::EmitArrayItems(std::ostream& fp, const Array::Ptr& val)
{
	bool first = true;

	ObjectLock olock(val);
	BOOST_FOREACH(const Value& item, val) {
		if (first)
			first = false;
		else
			fp << ", ";

		EmitValue(fp, 0, item);
	}
}

void ConfigWriter::EmitScope(std::ostream& fp, int indentLevel, const Dictionary::Ptr& val, const Array::Ptr& imports)
{
	fp << "{";

	if (imports && imports->GetLength() > 0) {
		ObjectLock xlock(imports);
		BOOST_FOREACH(const Value& import, imports) {
			fp << "\n";
			EmitIndent(fp, indentLevel);
			fp << "import \"" << import << "\"";
		}

		fp << "\n";
	}

	if (val) {
		ObjectLock olock(val);
		BOOST_FOREACH(const Dictionary::Pair& kv, val) {
			fp << "\n";
			EmitIndent(fp, indentLevel);
			
			std::vector<String> tokens;
			boost::algorithm::split(tokens, kv.first, boost::is_any_of("."));
			
			EmitIdentifier(fp, tokens[0], true);
			
			for (std::vector<String>::size_type i = 1; i < tokens.size(); i++) {
				fp << "[";
				EmitString(fp, tokens[i]);
				fp << "]";
			}
			
			fp << " = ";
			EmitValue(fp, indentLevel + 1, kv.second);
		}
	}

	fp << "\n";
	EmitIndent(fp, indentLevel - 1);
	fp << "}";
}

void ConfigWriter::EmitValue(std::ostream& fp, int indentLevel, const Value& val)
{
	if (val.IsObjectType<Array>())
		EmitArray(fp, val);
	else if (val.IsObjectType<Dictionary>())
		EmitScope(fp, indentLevel, val);
	else if (val.IsString())
		EmitString(fp, val);
	else if (val.IsNumber())
		EmitNumber(fp, val);
	else if (val.IsBoolean())
		EmitBoolean(fp, val);
	else if (val.IsEmpty())
		EmitEmpty(fp);
}

void ConfigWriter::EmitRaw(std::ostream& fp, const String& val)
{
	fp << val;
}

void ConfigWriter::EmitIndent(std::ostream& fp, int indentLevel)
{
	for (int i = 0; i < indentLevel; i++)
		fp << "\t";
}

void ConfigWriter::EmitIdentifier(std::ostream& fp, const String& identifier, bool inAssignment)
{
	static std::set<String> keywords;
	if (keywords.empty()) {
		const std::vector<String>& vkeywords = ConfigCompiler::GetKeywords();
		std::copy(vkeywords.begin(), vkeywords.end(), std::inserter(keywords, keywords.begin()));
	}

	if (keywords.find(identifier) != keywords.end()) {
		fp << "@" << identifier;
		return;
	}

	boost::regex expr("^[a-zA-Z_][a-zA-Z0-9\\_]*$");
	boost::smatch what;
	if (boost::regex_search(identifier.GetData(), what, expr))
		fp << identifier;
	else if (inAssignment)
		EmitString(fp, identifier);
	else
		BOOST_THROW_EXCEPTION(std::invalid_argument("Invalid identifier"));
}

void ConfigWriter::EmitConfigItem(std::ostream& fp, const String& type, const String& name, bool isTemplate,
    const Array::Ptr& imports, const Dictionary::Ptr& attrs)
{
	if (isTemplate)
		fp << "template ";
	else
		fp << "object ";

	EmitIdentifier(fp, type, false);
	fp << " ";
	EmitString(fp, name);
	fp << " ";
	EmitScope(fp, 1, attrs, imports);
}

void ConfigWriter::EmitComment(std::ostream& fp, const String& text)
{
	fp << "/* " << text << " */\n";
}

void ConfigWriter::EmitFunctionCall(std::ostream& fp, const String& name, const Array::Ptr& arguments)
{
	EmitIdentifier(fp, name, false);
	fp << "(";
	EmitArrayItems(fp, arguments);
	fp << ")";
}

String ConfigWriter::EscapeIcingaString(const String& str)
{
	String result = str;
	boost::algorithm::replace_all(result, "\\", "\\\\");
	boost::algorithm::replace_all(result, "\n", "\\n");
	boost::algorithm::replace_all(result, "\t", "\\t");
	boost::algorithm::replace_all(result, "\r", "\\r");
	boost::algorithm::replace_all(result, "\b", "\\b");
	boost::algorithm::replace_all(result, "\f", "\\f");
	boost::algorithm::replace_all(result, "\"", "\\\"");
	return result;
}
