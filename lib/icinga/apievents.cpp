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

#include "icinga/apievents.hpp"
#include "icinga/service.hpp"
#include "icinga/perfdatavalue.hpp"
#include "remote/apilistener.hpp"
#include "remote/endpoint.hpp"
#include "remote/messageorigin.hpp"
#include "remote/zone.hpp"
#include "remote/apifunction.hpp"
#include "base/application.hpp"
#include "base/configtype.hpp"
#include "base/utility.hpp"
#include "base/exception.hpp"
#include "base/initialize.hpp"
#include "base/serializer.hpp"
#include "base/json.hpp"
#include <fstream>

using namespace icinga;

INITIALIZE_ONCE(&ApiEvents::StaticInitialize);

REGISTER_APIFUNCTION(CheckResult, event, &ApiEvents::CheckResultAPIHandler);
REGISTER_APIFUNCTION(SetNextCheck, event, &ApiEvents::NextCheckChangedAPIHandler);
REGISTER_APIFUNCTION(SetNextNotification, event, &ApiEvents::NextNotificationChangedAPIHandler);
REGISTER_APIFUNCTION(SetForceNextCheck, event, &ApiEvents::ForceNextCheckChangedAPIHandler);
REGISTER_APIFUNCTION(SetForceNextNotification, event, &ApiEvents::ForceNextNotificationChangedAPIHandler);
REGISTER_APIFUNCTION(SetEnableActiveChecks, event, &ApiEvents::EnableActiveChecksChangedAPIHandler);
REGISTER_APIFUNCTION(SetEnablePassiveChecks, event, &ApiEvents::EnablePassiveChecksChangedAPIHandler);
REGISTER_APIFUNCTION(SetEnableNotifications, event, &ApiEvents::EnableNotificationsChangedAPIHandler);
REGISTER_APIFUNCTION(SetEnableFlapping, event, &ApiEvents::EnableFlappingChangedAPIHandler);
REGISTER_APIFUNCTION(SetEnableEventHandler, event, &ApiEvents::EnableEventHandlerChangedAPIHandler);
REGISTER_APIFUNCTION(SetEnablePerfdata, event, &ApiEvents::EnablePerfdataChangedAPIHandler);
REGISTER_APIFUNCTION(SetCheckInterval, event, &ApiEvents::CheckIntervalChangedAPIHandler);
REGISTER_APIFUNCTION(SetRetryInterval, event, &ApiEvents::RetryIntervalChangedAPIHandler);
REGISTER_APIFUNCTION(SetMaxCheckAttempts, event, &ApiEvents::MaxCheckAttemptsChangedAPIHandler);
REGISTER_APIFUNCTION(SetEventCommand, event, &ApiEvents::EventCommandChangedAPIHandler);
REGISTER_APIFUNCTION(SetCheckCommand, event, &ApiEvents::CheckCommandChangedAPIHandler);
REGISTER_APIFUNCTION(SetCheckPeriod, event, &ApiEvents::CheckPeriodChangedAPIHandler);
REGISTER_APIFUNCTION(SetVars, event, &ApiEvents::VarsChangedAPIHandler);
REGISTER_APIFUNCTION(AddComment, event, &ApiEvents::CommentAddedAPIHandler);
REGISTER_APIFUNCTION(RemoveComment, event, &ApiEvents::CommentRemovedAPIHandler);
REGISTER_APIFUNCTION(AddDowntime, event, &ApiEvents::DowntimeAddedAPIHandler);
REGISTER_APIFUNCTION(RemoveDowntime, event, &ApiEvents::DowntimeRemovedAPIHandler);
REGISTER_APIFUNCTION(SetAcknowledgement, event, &ApiEvents::AcknowledgementSetAPIHandler);
REGISTER_APIFUNCTION(ClearAcknowledgement, event, &ApiEvents::AcknowledgementClearedAPIHandler);
REGISTER_APIFUNCTION(UpdateRepository, event, &ApiEvents::UpdateRepositoryAPIHandler);
REGISTER_APIFUNCTION(ExecuteCommand, event, &ApiEvents::ExecuteCommandAPIHandler);

static Timer::Ptr l_RepositoryTimer;

void ApiEvents::StaticInitialize(void)
{
	Checkable::OnNewCheckResult.connect(&ApiEvents::CheckResultHandler);
	Checkable::OnNextCheckChanged.connect(&ApiEvents::NextCheckChangedHandler);
	Notification::OnNextNotificationChanged.connect(&ApiEvents::NextNotificationChangedHandler);
	Checkable::OnForceNextCheckChanged.connect(&ApiEvents::ForceNextCheckChangedHandler);
	Checkable::OnForceNextNotificationChanged.connect(&ApiEvents::ForceNextNotificationChangedHandler);
	Checkable::OnEnableActiveChecksChanged.connect(&ApiEvents::EnableActiveChecksChangedHandler);
	Checkable::OnEnablePassiveChecksChanged.connect(&ApiEvents::EnablePassiveChecksChangedHandler);
	Checkable::OnEnableNotificationsChanged.connect(&ApiEvents::EnableNotificationsChangedHandler);
	Checkable::OnEnableFlappingChanged.connect(&ApiEvents::EnableFlappingChangedHandler);
	Checkable::OnEnableEventHandlerChanged.connect(&ApiEvents::EnableEventHandlerChangedHandler);
	Checkable::OnEnablePerfdataChanged.connect(&ApiEvents::EnablePerfdataChangedHandler);
	Checkable::OnCheckIntervalChanged.connect(&ApiEvents::CheckIntervalChangedHandler);
	Checkable::OnRetryIntervalChanged.connect(&ApiEvents::RetryIntervalChangedHandler);
	Checkable::OnMaxCheckAttemptsChanged.connect(&ApiEvents::MaxCheckAttemptsChangedHandler);
	Checkable::OnEventCommandRawChanged.connect(&ApiEvents::EventCommandChangedHandler);
	Checkable::OnCheckCommandRawChanged.connect(&ApiEvents::CheckCommandChangedHandler);
	Checkable::OnCheckPeriodRawChanged.connect(&ApiEvents::CheckPeriodChangedHandler);
	Checkable::OnVarsChanged.connect(&ApiEvents::VarsChangedHandler);
	Checkable::OnCommentAdded.connect(&ApiEvents::CommentAddedHandler);
	Checkable::OnCommentRemoved.connect(&ApiEvents::CommentRemovedHandler);
	Checkable::OnDowntimeAdded.connect(&ApiEvents::DowntimeAddedHandler);
	Checkable::OnDowntimeRemoved.connect(&ApiEvents::DowntimeRemovedHandler);
	Checkable::OnAcknowledgementSet.connect(&ApiEvents::AcknowledgementSetHandler);
	Checkable::OnAcknowledgementCleared.connect(&ApiEvents::AcknowledgementClearedHandler);

	l_RepositoryTimer = new Timer();
	l_RepositoryTimer->SetInterval(30);
	l_RepositoryTimer->OnTimerExpired.connect(boost::bind(&ApiEvents::RepositoryTimerHandler));
	l_RepositoryTimer->Start();
	l_RepositoryTimer->Reschedule(0);
}

Dictionary::Ptr ApiEvents::MakeCheckResultMessage(const Checkable::Ptr& checkable, const CheckResult::Ptr& cr)
{
	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::CheckResult");

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	else {
		Value agent_service_name = checkable->GetExtension("agent_service_name");

		if (!agent_service_name.IsEmpty())
			params->Set("service", agent_service_name);
	}
	params->Set("cr", Serialize(cr));

	message->Set("params", params);

	return message;
}

void ApiEvents::CheckResultHandler(const Checkable::Ptr& checkable, const CheckResult::Ptr& cr, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Dictionary::Ptr message = MakeCheckResultMessage(checkable, cr);
	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::CheckResultAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'check result' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	CheckResult::Ptr cr = new CheckResult();

	Dictionary::Ptr vcr = params->Get("cr");
	Array::Ptr vperf = vcr->Get("performance_data");
	vcr->Remove("performance_data");

	Deserialize(cr, params->Get("cr"), true);

	Array::Ptr rperf = new Array();

	if (vperf) {
		ObjectLock olock(vperf);
		BOOST_FOREACH(const Value& vp, vperf) {
			Value p;

			if (vp.IsObjectType<Dictionary>()) {
				PerfdataValue::Ptr val = new PerfdataValue();
				Deserialize(val, vp, true);
				rperf->Add(val);
			} else
				rperf->Add(vp);
		}
	}

	cr->SetPerformanceData(rperf);

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable) && endpoint != checkable->GetCommandEndpoint()) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'check result' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	if (endpoint == checkable->GetCommandEndpoint())
		checkable->ProcessCheckResult(cr);
	else
		checkable->ProcessCheckResult(cr, origin);

	return Empty;
}

void ApiEvents::NextCheckChangedHandler(const Checkable::Ptr& checkable, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("next_check", checkable->GetNextCheck());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetNextCheck");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::NextCheckChangedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'next check changed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'next check changed' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	checkable->SetNextCheck(params->Get("next_check"), false, origin);

	return Empty;
}

void ApiEvents::NextNotificationChangedHandler(const Notification::Ptr& notification, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Dictionary::Ptr params = new Dictionary();
	params->Set("notification", notification->GetName());
	params->Set("next_notification", notification->GetNextNotification());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetNextNotification");
	message->Set("params", params);

	listener->RelayMessage(origin, notification, message, true);
}

Value ApiEvents::NextNotificationChangedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'next notification changed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Notification::Ptr notification = Notification::GetByName(params->Get("notification"));

	if (!notification)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(notification)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'next notification changed' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	notification->SetNextNotification(params->Get("next_notification"), false, origin);

	return Empty;
}

void ApiEvents::ForceNextCheckChangedHandler(const Checkable::Ptr& checkable, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("forced", checkable->GetForceNextCheck());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetForceNextCheck");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::ForceNextCheckChangedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'force next check changed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'force next check' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	checkable->SetForceNextCheck(params->Get("forced"), false, origin);

	return Empty;
}

void ApiEvents::ForceNextNotificationChangedHandler(const Checkable::Ptr& checkable, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("forced", checkable->GetForceNextNotification());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetForceNextNotification");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::ForceNextNotificationChangedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'force next notification changed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'force next notification' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	checkable->SetForceNextNotification(params->Get("forced"), false, origin);

	return Empty;
}


void ApiEvents::EnableActiveChecksChangedHandler(const Checkable::Ptr& checkable, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("enabled", checkable->GetEnableActiveChecks());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetEnableActiveChecks");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::EnableActiveChecksChangedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'enable active checks changed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'enable active checks' changed message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	checkable->SetEnableActiveChecks(params->Get("enabled"), false, origin);

	return Empty;
}

void ApiEvents::EnablePassiveChecksChangedHandler(const Checkable::Ptr& checkable, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("enabled", checkable->GetEnablePassiveChecks());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetEnablePassiveChecks");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::EnablePassiveChecksChangedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'enable passive checks changed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'enable passive checks changed' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	checkable->SetEnablePassiveChecks(params->Get("enabled"), false, origin);

	return Empty;
}

void ApiEvents::EnableNotificationsChangedHandler(const Checkable::Ptr& checkable, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("enabled", checkable->GetEnableNotifications());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetEnableNotifications");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::EnableNotificationsChangedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'enable notifications changed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'enable notifications changed' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	checkable->SetEnableNotifications(params->Get("enabled"), false, origin);

	return Empty;
}

void ApiEvents::EnableFlappingChangedHandler(const Checkable::Ptr& checkable, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("enabled", checkable->GetEnableFlapping());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetEnableFlapping");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::EnableFlappingChangedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'enable flapping changed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'enable flapping changed' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	checkable->SetEnableFlapping(params->Get("enabled"), false, origin);

	return Empty;
}

void ApiEvents::EnableEventHandlerChangedHandler(const Checkable::Ptr& checkable, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("enabled", checkable->GetEnableEventHandler());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetEnableEventHandler");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::EnableEventHandlerChangedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'enable event handler changed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'enable event handler' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	checkable->SetEnableEventHandler(params->Get("enabled"), false, origin);

	return Empty;
}

void ApiEvents::EnablePerfdataChangedHandler(const Checkable::Ptr& checkable, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("enabled", checkable->GetEnablePerfdata());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetEnablePerfdata");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::EnablePerfdataChangedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'enable perfdata changed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'enable perfdata changed' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	checkable->SetEnablePerfdata(params->Get("enabled"), false, origin);

	return Empty;
}

void ApiEvents::CheckIntervalChangedHandler(const Checkable::Ptr& checkable, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("interval", checkable->GetCheckInterval());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetCheckInterval");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::CheckIntervalChangedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'check interval changed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'check interval' changed message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	checkable->SetCheckInterval(params->Get("interval"), false, origin);

	return Empty;
}

void ApiEvents::RetryIntervalChangedHandler(const Checkable::Ptr& checkable, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("interval", checkable->GetRetryInterval());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetRetryInterval");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::RetryIntervalChangedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'retry interval changed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'retry interval' changed message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	checkable->SetRetryInterval(params->Get("interval"), false, origin);

	return Empty;
}

void ApiEvents::MaxCheckAttemptsChangedHandler(const Checkable::Ptr& checkable, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("attempts", checkable->GetMaxCheckAttempts());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetMaxCheckAttempts");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::MaxCheckAttemptsChangedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'max checkt attempts changed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'max check attempts changed' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	checkable->SetMaxCheckAttempts(params->Get("attempts"), false, origin);

	return Empty;
}

void ApiEvents::EventCommandChangedHandler(const Checkable::Ptr& checkable, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("command", checkable->GetEventCommand());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetEventCommand");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::EventCommandChangedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'event command changed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	EventCommand::Ptr command = EventCommand::GetByName(params->Get("command"));

	if (!command)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'event command changed' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	checkable->SetEventCommandRaw(command->GetName(), false, origin);

	return Empty;
}

void ApiEvents::CheckCommandChangedHandler(const Checkable::Ptr& checkable, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("command", checkable->GetCheckCommand());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetCheckCommand");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::CheckCommandChangedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'check command changed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'check command changed' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	CheckCommand::Ptr command = CheckCommand::GetByName(params->Get("command"));

	if (!command)
		return Empty;

	checkable->SetCheckCommandRaw(command->GetName(), false, origin);

	return Empty;
}

void ApiEvents::CheckPeriodChangedHandler(const Checkable::Ptr& checkable, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("timeperiod", checkable->GetCheckPeriod());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetCheckPeriod");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::CheckPeriodChangedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'check period changed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'check period changed' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	TimePeriod::Ptr timeperiod = TimePeriod::GetByName(params->Get("timeperiod"));

	if (!timeperiod)
		return Empty;

	checkable->SetCheckPeriodRaw(timeperiod->GetName(), false, origin);

	return Empty;
}

void ApiEvents::VarsChangedHandler(const CustomVarObject::Ptr& object, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Dictionary::Ptr params = new Dictionary();
	params->Set("object", object->GetName());

	ConfigType::Ptr dtype = object->GetType();
	ASSERT(dtype);

	params->Set("object_type", dtype->GetName());
	Log(LogDebug, "ApiEvents")
	    << "Changed vars handler for object name: '" << object->GetName() << "' type: '" << dtype->GetName() << "'.";

	params->Set("vars", Serialize(object->GetVars()));

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetVars");
	message->Set("params", params);

	listener->RelayMessage(origin, object, message, true);
}

Value ApiEvents::VarsChangedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'vars changed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	String objectName = params->Get("object");
	String objectType = params->Get("object_type");

	if (objectName.IsEmpty())
		return Empty;

	CustomVarObject::Ptr object;

	if (objectType.IsEmpty()) {
		/* keep the old broken way for compatibility reasons for <= v2.3.5 */
		object = Host::GetByName(objectName);
		if (!object)
			object = Service::GetByName(objectName);
		if (!object)
			object = User::GetByName(objectName);
		if (!object)
			object = Service::GetByName(objectName);
		if (!object)
			object = EventCommand::GetByName(objectName);
		if (!object)
			object = CheckCommand::GetByName(objectName);
		if (!object)
			object = NotificationCommand::GetByName(objectName);
	} else {
		ConfigType::Ptr dtype = ConfigType::GetByName(objectType);

		if (!dtype)
			return Empty;

		object = dynamic_pointer_cast<CustomVarObject>(dtype->GetObject(objectName));
	}

	if (!object)
		return Empty;

	Log(LogDebug, "ApiEvents")
	    << "Processing 'vars changed' for object: '" << object->GetName() << "' type: '" << object->GetType()->GetName() << "'.";

	if (origin->FromZone && !origin->FromZone->CanAccessObject(object)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'vars changed' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	Dictionary::Ptr vars = params->Get("vars");

	if (!vars)
		return Empty;

	object->SetVars(vars, false, origin);

	return Empty;
}

void ApiEvents::CommentAddedHandler(const Checkable::Ptr& checkable, const Comment::Ptr& comment, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("comment", Serialize(comment));

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::AddComment");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::CommentAddedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'comment added' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'comment added' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	Comment::Ptr comment = new Comment();
	Deserialize(comment, params->Get("comment"), true);

	checkable->AddComment(comment->GetEntryType(), comment->GetAuthor(),
	    comment->GetText(), comment->GetExpireTime(), comment->GetName(), origin);

	return Empty;
}

void ApiEvents::CommentRemovedHandler(const Checkable::Ptr& checkable, const Comment::Ptr& comment, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("id", comment->GetName());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::RemoveComment");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::CommentRemovedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'comment removed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'comment removed' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	checkable->RemoveComment(params->Get("id"), origin);

	return Empty;
}

void ApiEvents::DowntimeAddedHandler(const Checkable::Ptr& checkable, const Downtime::Ptr& downtime, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("downtime", Serialize(downtime));

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::AddDowntime");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::DowntimeAddedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'downtime added' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'downtime added' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	Downtime::Ptr downtime = new Downtime();
	Deserialize(downtime, params->Get("downtime"), true);

	checkable->AddDowntime(downtime->GetAuthor(), downtime->GetComment(),
	    downtime->GetStartTime(), downtime->GetEndTime(),
	    downtime->GetFixed(), downtime->GetTriggeredBy(),
	    downtime->GetDuration(), downtime->GetScheduledBy(),
	    downtime->GetName(), origin);

	return Empty;
}

void ApiEvents::DowntimeRemovedHandler(const Checkable::Ptr& checkable, const Downtime::Ptr& downtime, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("id", downtime->GetName());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::RemoveDowntime");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::DowntimeRemovedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'downtime removed' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'downtime removed' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	checkable->RemoveDowntime(params->Get("id"), false, origin);

	return Empty;
}

void ApiEvents::AcknowledgementSetHandler(const Checkable::Ptr& checkable,
    const String& author, const String& comment, AcknowledgementType type,
    bool notify, double expiry, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("author", author);
	params->Set("comment", comment);
	params->Set("acktype", type);
	params->Set("notify", notify);
	params->Set("expiry", expiry);

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetAcknowledgement");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::AcknowledgementSetAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'acknowledgement set' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'acknowledgement set' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	checkable->AcknowledgeProblem(params->Get("author"), params->Get("comment"),
	    static_cast<AcknowledgementType>(static_cast<int>(params->Get("acktype"))),
	    params->Get("notify"), params->Get("expiry"), origin);

	return Empty;
}

void ApiEvents::AcknowledgementClearedHandler(const Checkable::Ptr& checkable, const MessageOrigin::Ptr& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = new Dictionary();
	params->Set("host", host->GetName());
	if (service)
		params->Set("service", service->GetShortName());

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::ClearAcknowledgement");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::AcknowledgementClearedAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr endpoint = origin->FromClient->GetEndpoint();

	if (!endpoint) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'acknowledgement cleared' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	if (!params)
		return Empty;

	Host::Ptr host = Host::GetByName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (origin->FromZone && !origin->FromZone->CanAccessObject(checkable)) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'acknowledgement cleared' message from '" << origin->FromClient->GetIdentity() << "': Unauthorized access.";
		return Empty;
	}

	checkable->ClearAcknowledgement(origin);

	return Empty;
}

Value ApiEvents::ExecuteCommandAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	Endpoint::Ptr sourceEndpoint = origin->FromClient->GetEndpoint();

	if (!sourceEndpoint || (origin->FromZone && !Zone::GetLocalZone()->IsChildOf(origin->FromZone))) {
		Log(LogNotice, "ApiEvents")
		    << "Discarding 'execute command' message from '" << origin->FromClient->GetIdentity() << "': Invalid endpoint origin (client not allowed).";
		return Empty;
	}

	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener) {
		Log(LogCritical, "ApiListener", "No instance available.");
		return Empty;
	}

	if (!listener->GetAcceptCommands()) {
		Log(LogWarning, "ApiListener")
		    << "Ignoring command. '" << listener->GetName() << "' does not accept commands.";

		Host::Ptr host = new Host();
		Dictionary::Ptr attrs = new Dictionary();

		attrs->Set("__name", params->Get("host"));
		attrs->Set("type", "Host");

		Deserialize(host, attrs, false, FAConfig);

		if (params->Contains("service"))
			host->SetExtension("agent_service_name", params->Get("service"));

		CheckResult::Ptr cr = new CheckResult();
		cr->SetState(ServiceUnknown);
		cr->SetOutput("Endpoint '" + Endpoint::GetLocalEndpoint()->GetName() + "' does not accept commands.");
		Dictionary::Ptr message = MakeCheckResultMessage(host, cr);
		listener->SyncSendMessage(sourceEndpoint, message);

		return Empty;
	}

	/* use a virtual host object for executing the command */
	Host::Ptr host = new Host();
	Dictionary::Ptr attrs = new Dictionary();

	attrs->Set("__name", params->Get("host"));
	attrs->Set("type", "Host");

	Deserialize(host, attrs, false, FAConfig);

	if (params->Contains("service"))
		host->SetExtension("agent_service_name", params->Get("service"));

	String command = params->Get("command");
	String command_type = params->Get("command_type");

	if (command_type == "check_command") {
		if (!CheckCommand::GetByName(command)) {
			CheckResult::Ptr cr = new CheckResult();
			cr->SetState(ServiceUnknown);
			cr->SetOutput("Check command '" + command + "' does not exist.");
			Dictionary::Ptr message = MakeCheckResultMessage(host, cr);
			listener->SyncSendMessage(sourceEndpoint, message);
			return Empty;
		}
	} else if (command_type == "event_command") {
		if (!EventCommand::GetByName(command)) {
			Log(LogWarning, "ApiEvents")
			    << "Event command '" << command << "' does not exist.";
			return Empty;
		}
	} else
		return Empty;

	attrs->Set(command_type, params->Get("command"));
	attrs->Set("command_endpoint", sourceEndpoint->GetName());

	Deserialize(host, attrs, false, FAConfig);

	host->SetExtension("agent_check", true);

	Dictionary::Ptr macros = params->Get("macros");

	if (command_type == "check_command") {
		try {
			host->ExecuteRemoteCheck(macros);
		} catch (const std::exception& ex) {
			CheckResult::Ptr cr = new CheckResult();
			cr->SetState(ServiceUnknown);

			String output = "Exception occured while checking '" + host->GetName() + "': " + DiagnosticInformation(ex);
			cr->SetOutput(output);

			double now = Utility::GetTime();
			cr->SetScheduleStart(now);
			cr->SetScheduleEnd(now);
			cr->SetExecutionStart(now);
			cr->SetExecutionEnd(now);

			Dictionary::Ptr message = MakeCheckResultMessage(host, cr);
			listener->SyncSendMessage(sourceEndpoint, message);

			Log(LogCritical, "checker", output);
		}
	} else if (command_type == "event_command") {
		host->ExecuteEventHandler(macros, true);
	}

	return Empty;
}

void ApiEvents::RepositoryTimerHandler(void)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Dictionary::Ptr repository = new Dictionary();

	BOOST_FOREACH(const Host::Ptr& host, ConfigType::GetObjectsByType<Host>()) {
		Array::Ptr services = new Array();

		BOOST_FOREACH(const Service::Ptr& service, host->GetServices()) {
			services->Add(service->GetShortName());
		}

		repository->Set(host->GetName(), services);
	}

	Endpoint::Ptr my_endpoint = Endpoint::GetLocalEndpoint();

	if (!my_endpoint) {
		Log(LogWarning, "ApiEvents", "No local endpoint defined. Bailing out.");
		return;
	}

	Zone::Ptr my_zone = my_endpoint->GetZone();

	if (!my_zone)
		return;

	Dictionary::Ptr params = new Dictionary();
	params->Set("seen", Utility::GetTime());
	params->Set("endpoint", my_endpoint->GetName());

	Zone::Ptr parent_zone = my_zone->GetParent();
	if (parent_zone)
		params->Set("parent_zone", parent_zone->GetName());

	params->Set("zone", my_zone->GetName());
	params->Set("repository", repository);

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::UpdateRepository");
	message->Set("params", params);

	listener->RelayMessage(MessageOrigin::Ptr(), my_zone, message, false);
}

String ApiEvents::GetRepositoryDir(void)
{
	return Application::GetLocalStateDir() + "/lib/icinga2/api/repository/";
}

Value ApiEvents::UpdateRepositoryAPIHandler(const MessageOrigin::Ptr& origin, const Dictionary::Ptr& params)
{
	if (!params)
		return Empty;

	Value vrepository = params->Get("repository");
	if (vrepository.IsEmpty() || !vrepository.IsObjectType<Dictionary>())
		return Empty;

	String repositoryFile = GetRepositoryDir() + SHA256(params->Get("endpoint")) + ".repo";
	String repositoryTempFile = repositoryFile + ".tmp";

	std::ofstream fp(repositoryTempFile.CStr(), std::ofstream::out | std::ostream::trunc);
	fp << JsonEncode(params);
	fp.close();

#ifdef _WIN32
	_unlink(repositoryFile.CStr());
#endif /* _WIN32 */

	if (rename(repositoryTempFile.CStr(), repositoryFile.CStr()) < 0) {
		BOOST_THROW_EXCEPTION(posix_error()
		    << boost::errinfo_api_function("rename")
		    << boost::errinfo_errno(errno)
		    << boost::errinfo_file_name(repositoryTempFile));
	}

	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return Empty;

	Dictionary::Ptr message = new Dictionary();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::UpdateRepository");
	message->Set("params", params);

	listener->RelayMessage(origin, Zone::GetLocalZone(), message, true);

	return Empty;
}

