#include <btcb/lib/jsonconfig.hpp>
#include <btcb/node/ipcconfig.hpp>

btcb::error btcb::ipc::ipc_config::serialize_json (btcb::jsonconfig & json) const
{
	btcb::jsonconfig tcp_l;
	// Only write out experimental config values if they're previously set explicitly in the config file
	if (transport_tcp.io_threads >= 0)
	{
		tcp_l.put ("io_threads", transport_tcp.io_threads);
	}
	tcp_l.put ("enable", transport_tcp.enabled);
	tcp_l.put ("port", transport_tcp.port);
	tcp_l.put ("io_timeout", transport_tcp.io_timeout);
	json.put_child ("tcp", tcp_l);

	btcb::jsonconfig domain_l;
	domain_l.put ("version", transport_domain.json_version ());
	if (transport_domain.io_threads >= 0)
	{
		domain_l.put ("io_threads", transport_domain.io_threads);
	}
	domain_l.put ("enable", transport_domain.enabled);
	domain_l.put ("allow_unsafe", transport_domain.allow_unsafe);
	domain_l.put ("path", transport_domain.path);
	domain_l.put ("io_timeout", transport_domain.io_timeout);
	json.put_child ("local", domain_l);
	return json.get_error ();
}

btcb::error btcb::ipc::ipc_config::deserialize_json (bool & upgraded_a, btcb::jsonconfig & json)
{
	auto tcp_l (json.get_optional_child ("tcp"));
	if (tcp_l)
	{
		tcp_l->get_optional<long> ("io_threads", transport_tcp.io_threads, -1);
		tcp_l->get_optional<bool> ("allow_unsafe", transport_tcp.allow_unsafe);
		tcp_l->get<bool> ("enable", transport_tcp.enabled);
		tcp_l->get<uint16_t> ("port", transport_tcp.port);
		tcp_l->get<size_t> ("io_timeout", transport_tcp.io_timeout);
	}

	auto domain_l (json.get_optional_child ("local"));
	if (domain_l)
	{
		auto version_l (domain_l->get_optional<unsigned> ("version"));
		if (!version_l)
		{
			version_l = 1;
			domain_l->put ("version", *version_l);
			domain_l->put ("allow_unsafe", transport_domain.allow_unsafe);
			upgraded_a = true;
		}

		domain_l->get_optional<long> ("io_threads", transport_domain.io_threads, -1);
		domain_l->get_optional<bool> ("allow_unsafe", transport_domain.allow_unsafe);
		domain_l->get<bool> ("enable", transport_domain.enabled);
		domain_l->get<std::string> ("path", transport_domain.path);
		domain_l->get<size_t> ("io_timeout", transport_domain.io_timeout);
	}

	return json.get_error ();
}
