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

#include "icinga/comment.hpp"
#include "icinga/comment.tcpp"
#include "icinga/host.hpp"
#include "base/utility.hpp"
#include "base/configtype.hpp"
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

using namespace icinga;

REGISTER_TYPE(Comment);

String CommentNameComposer::MakeName(const String& shortName, const Object::Ptr& context) const
{
	Comment::Ptr downtime = dynamic_pointer_cast<Comment>(context);

	if (!downtime)
		return "";

	String name = downtime->GetHostName();

	if (!downtime->GetServiceName().IsEmpty())
		name += "!" + downtime->GetServiceName();

	name += "!" + shortName;

	return name;
}

Dictionary::Ptr CommentNameComposer::ParseName(const String& name) const
{
	std::vector<String> tokens;
	boost::algorithm::split(tokens, name, boost::is_any_of("!"));

	if (tokens.size() < 2)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Invalid Comment name."));

	Dictionary::Ptr result = new Dictionary();
	result->Set("host_name", tokens[0]);

	if (tokens.size() > 2) {
		result->Set("service_name", tokens[1]);
		result->Set("name", tokens[2]);
	} else {
		result->Set("name", tokens[1]);
	}

	return result;
}

void Comment::OnAllConfigLoaded(void)
{
	ConfigObject::OnAllConfigLoaded();

	if (!GetCheckable())
		BOOST_THROW_EXCEPTION(ScriptError("Comment '" + GetName() + "' references a host/service which doesn't exist.", GetDebugInfo()));
}

Checkable::Ptr Comment::GetCheckable(void) const
{
	Host::Ptr host = Host::GetByName(GetHostName());

	if (GetServiceName().IsEmpty())
		return host;
	else
		return host->GetServiceByShortName(GetServiceName());
}

bool Comment::IsExpired(void) const
{
	double expire_time = GetExpireTime();

	return (expire_time != 0 && expire_time < Utility::GetTime());
}
