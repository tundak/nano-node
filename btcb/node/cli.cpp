#include <btcb/lib/config.hpp>
#include <btcb/lib/interface.h>
#include <btcb/node/cli.hpp>
#include <btcb/node/common.hpp>
#include <btcb/node/daemonconfig.hpp>
#include <btcb/node/node.hpp>

namespace
{
void reset_confirmation_heights (btcb::block_store & store);
}

std::string btcb::error_cli_messages::message (int ev) const
{
	switch (static_cast<btcb::error_cli> (ev))
	{
		case btcb::error_cli::generic:
			return "Unknown error";
		case btcb::error_cli::parse_error:
			return "Coud not parse command line";
		case btcb::error_cli::invalid_arguments:
			return "Invalid arguments";
		case btcb::error_cli::unknown_command:
			return "Unknown command";
	}

	return "Invalid error code";
}

void btcb::add_node_options (boost::program_options::options_description & description_a)
{
	// clang-format off
	description_a.add_options ()
	("account_create", "Insert next deterministic key in to <wallet>")
	("account_get", "Get account number for the <key>")
	("account_key", "Get the public key for <account>")
	("vacuum", "Compact database. If data_path is missing, the database in data directory is compacted.")
	("snapshot", "Compact database and create snapshot, functions similar to vacuum but does not replace the existing database")
	("data_path", boost::program_options::value<std::string> (), "Use the supplied path as the data directory")
	("network", boost::program_options::value<std::string> (), "Use the supplied network (live, beta or test)")
	("clear_send_ids", "Remove all send IDs from the database (dangerous: not intended for production use)")
	("online_weight_clear", "Clear online weight history records")
	("peer_clear", "Clear online peers database dump")
	("unchecked_clear", "Clear unchecked blocks")
	("confirmation_height_clear", "Clear confirmation height")
	("diagnostics", "Run internal diagnostics")
	("key_create", "Generates a adhoc random keypair and prints it to stdout")
	("key_expand", "Derive public key and account number from <key>")
	("wallet_add_adhoc", "Insert <key> in to <wallet>")
	("wallet_create", "Creates a new wallet and prints the ID")
	("wallet_change_seed", "Changes seed for <wallet> to <key>")
	("wallet_decrypt_unsafe", "Decrypts <wallet> using <password>, !!THIS WILL PRINT YOUR PRIVATE KEY TO STDOUT!!")
	("wallet_destroy", "Destroys <wallet> and all keys it contains")
	("wallet_import", "Imports keys in <file> using <password> in to <wallet>")
	("wallet_list", "Dumps wallet IDs and public keys")
	("wallet_remove", "Remove <account> from <wallet>")
	("wallet_representative_get", "Prints default representative for <wallet>")
	("wallet_representative_set", "Set <account> as default representative for <wallet>")
	("vote_dump", "Dump most recent votes from representatives")
	("account", boost::program_options::value<std::string> (), "Defines <account> for other commands")
	("file", boost::program_options::value<std::string> (), "Defines <file> for other commands")
	("key", boost::program_options::value<std::string> (), "Defines the <key> for other commands, hex")
	("seed", boost::program_options::value<std::string> (), "Defines the <seed> for other commands, hex")
	("password", boost::program_options::value<std::string> (), "Defines <password> for other commands")
	("wallet", boost::program_options::value<std::string> (), "Defines <wallet> for other commands")
	("force", boost::program_options::value<bool>(), "Bool to force command if allowed");
	// clang-format on
}

std::error_code btcb::handle_node_options (boost::program_options::variables_map & vm)
{
	std::error_code ec;
	boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : btcb::working_path ();

	if (vm.count ("account_create"))
	{
		if (vm.count ("wallet") == 1)
		{
			btcb::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
			{
				std::string password;
				if (vm.count ("password") > 0)
				{
					password = vm["password"].as<std::string> ();
				}
				inactive_node node (data_path);
				auto wallet (node.node->wallets.open (wallet_id));
				if (wallet != nullptr)
				{
					auto transaction (wallet->wallets.tx_begin_write ());
					if (!wallet->enter_password (transaction, password))
					{
						auto pub (wallet->store.deterministic_insert (transaction));
						std::cout << boost::str (boost::format ("Account: %1%\n") % pub.to_account ());
					}
					else
					{
						std::cerr << "Invalid password\n";
						ec = btcb::error_cli::invalid_arguments;
					}
				}
				else
				{
					std::cerr << "Wallet doesn't exist\n";
					ec = btcb::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id\n";
				ec = btcb::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "wallet_add command requires one <wallet> option and one <key> option and optionally one <password> option\n";
			ec = btcb::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("account_get") > 0)
	{
		if (vm.count ("key") == 1)
		{
			btcb::uint256_union pub;
			pub.decode_hex (vm["key"].as<std::string> ());
			std::cout << "Account: " << pub.to_account () << std::endl;
		}
		else
		{
			std::cerr << "account comand requires one <key> option\n";
			ec = btcb::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("account_key") > 0)
	{
		if (vm.count ("account") == 1)
		{
			btcb::uint256_union account;
			account.decode_account (vm["account"].as<std::string> ());
			std::cout << "Hex: " << account.to_string () << std::endl;
		}
		else
		{
			std::cerr << "account_key command requires one <account> option\n";
			ec = btcb::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("vacuum") > 0)
	{
		try
		{
			auto vacuum_path = data_path / "vacuumed.ldb";
			auto source_path = data_path / "data.ldb";
			auto backup_path = data_path / "backup.vacuum.ldb";

			std::cout << "Vacuuming database copy in " << data_path << std::endl;
			std::cout << "This may take a while..." << std::endl;

			// Scope the node so the mdb environment gets cleaned up properly before
			// the original file is replaced with the vacuumed file.
			bool success = false;
			{
				inactive_node node (data_path);
				if (vm.count ("unchecked_clear"))
				{
					auto transaction (node.node->store.tx_begin_write ());
					node.node->store.unchecked_clear (transaction);
				}
				if (vm.count ("clear_send_ids"))
				{
					auto transaction (node.node->wallets.tx_begin_write ());
					node.node->wallets.clear_send_ids (transaction);
				}
				if (vm.count ("online_weight_clear"))
				{
					auto transaction (node.node->store.tx_begin_write ());
					node.node->store.online_weight_clear (transaction);
				}
				if (vm.count ("peer_clear"))
				{
					auto transaction (node.node->store.tx_begin_write ());
					node.node->store.peer_clear (transaction);
				}
				success = node.node->copy_with_compaction (vacuum_path);
			}

			if (success)
			{
				// Note that these throw on failure
				std::cout << "Finalizing" << std::endl;
				boost::filesystem::remove (backup_path);
				boost::filesystem::rename (source_path, backup_path);
				boost::filesystem::rename (vacuum_path, source_path);
				std::cout << "Vacuum completed" << std::endl;
			}
			else
			{
				std::cerr << "Vacuum failed (copy_with_compaction returned false)" << std::endl;
			}
		}
		catch (const boost::filesystem::filesystem_error & ex)
		{
			std::cerr << "Vacuum failed during a file operation: " << ex.what () << std::endl;
		}
		catch (...)
		{
			std::cerr << "Vacuum failed (unknown reason)" << std::endl;
		}
	}
	else if (vm.count ("snapshot"))
	{
		try
		{
			boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : btcb::working_path ();

			auto source_path = data_path / "data.ldb";
			auto snapshot_path = data_path / "snapshot.ldb";

			std::cout << "Database snapshot of " << source_path << " to " << snapshot_path << " in progress" << std::endl;
			std::cout << "This may take a while..." << std::endl;

			bool success = false;
			{
				inactive_node node (data_path);
				if (vm.count ("unchecked_clear"))
				{
					auto transaction (node.node->store.tx_begin_write ());
					node.node->store.unchecked_clear (transaction);
				}
				if (vm.count ("clear_send_ids"))
				{
					auto transaction (node.node->wallets.tx_begin_write ());
					node.node->wallets.clear_send_ids (transaction);
				}
				if (vm.count ("online_weight_clear"))
				{
					auto transaction (node.node->store.tx_begin_write ());
					node.node->store.online_weight_clear (transaction);
				}
				if (vm.count ("peer_clear"))
				{
					auto transaction (node.node->store.tx_begin_write ());
					node.node->store.peer_clear (transaction);
				}
				if (vm.count ("confirmation_height_clear"))
				{
					reset_confirmation_heights (node.node->store);
				}

				success = node.node->copy_with_compaction (snapshot_path);
			}
			if (success)
			{
				std::cout << "Snapshot completed, This can be found at " << snapshot_path << std::endl;
			}
			else
			{
				std::cerr << "Snapshot Failed (copy_with_compaction returned false)" << std::endl;
			}
		}
		catch (const boost::filesystem::filesystem_error & ex)
		{
			std::cerr << "Snapshot failed during a file operation: " << ex.what () << std::endl;
		}
		catch (...)
		{
			std::cerr << "Snapshot Failed (unknown reason)" << std::endl;
		}
	}
	else if (vm.count ("unchecked_clear"))
	{
		boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : btcb::working_path ();
		inactive_node node (data_path);
		auto transaction (node.node->store.tx_begin_write ());
		node.node->store.unchecked_clear (transaction);
		std::cout << "Unchecked blocks deleted" << std::endl;
	}
	else if (vm.count ("clear_send_ids"))
	{
		boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : btcb::working_path ();
		inactive_node node (data_path);
		auto transaction (node.node->wallets.tx_begin_write ());
		node.node->wallets.clear_send_ids (transaction);
		std::cout << "Send IDs deleted" << std::endl;
	}
	else if (vm.count ("online_weight_clear"))
	{
		boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : btcb::working_path ();
		inactive_node node (data_path);
		auto transaction (node.node->store.tx_begin_write ());
		node.node->store.online_weight_clear (transaction);
		std::cout << "Onine weight records are removed" << std::endl;
	}
	else if (vm.count ("peer_clear"))
	{
		boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : btcb::working_path ();
		inactive_node node (data_path);
		auto transaction (node.node->store.tx_begin_write ());
		node.node->store.peer_clear (transaction);
		std::cout << "Database peers are removed" << std::endl;
	}
	else if (vm.count ("confirmation_height_clear"))
	{
		boost::filesystem::path data_path = vm.count ("data_path") ? boost::filesystem::path (vm["data_path"].as<std::string> ()) : btcb::working_path ();
		inactive_node node (data_path);
		auto account_it = vm.find ("account");
		if (account_it != vm.cend ())
		{
			auto account_str = account_it->second.as<std::string> ();
			btcb::account account;
			if (!account.decode_account (account_str))
			{
				btcb::account_info account_info;
				auto transaction (node.node->store.tx_begin_read ());
				if (!node.node->store.account_get (transaction, account, account_info))
				{
					auto transaction (node.node->store.tx_begin_write ());
					node.node->store.confirmation_height_clear (transaction, account, account_info);
					std::cout << "Confirmation height of account " << account_str << " is set to 0" << std::endl;
				}
				else
				{
					std::cerr << "Could not find account" << std::endl;
					ec = btcb::error_cli::generic;
				}
			}
			else
			{
				std::cerr << "Invalid account id\n";
				ec = btcb::error_cli::invalid_arguments;
			}
		}
		else
		{
			reset_confirmation_heights (node.node->store);
			std::cout << "Confirmation heights of all accounts (except genesis) are set to 0" << std::endl;
		}
	}
	else if (vm.count ("diagnostics"))
	{
		inactive_node node (data_path);

		// Check/upgrade the config.json file.
		{
			btcb::daemon_config config (data_path);
			auto error = btcb::read_and_update_daemon_config (data_path, config);
			if (error)
			{
				std::cerr << "Error deserializing config: " << error.get_message () << std::endl;
			}
		}

		std::cout << "Testing hash function" << std::endl;
		btcb::raw_key key;
		key.data.clear ();
		btcb::send_block send (0, 0, 0, key, 0, 0);
		std::cout << "Testing key derivation function" << std::endl;
		btcb::raw_key junk1;
		junk1.data.clear ();
		btcb::uint256_union junk2 (0);
		btcb::kdf kdf;
		kdf.phs (junk1, "", junk2);
		std::cout << "Dumping OpenCL information" << std::endl;
		bool error (false);
		btcb::opencl_environment environment (error);
		if (!error)
		{
			environment.dump (std::cout);
			std::stringstream stream;
			environment.dump (stream);
			node.node->logger.always_log (stream.str ());
		}
		else
		{
			std::cerr << "Error initializing OpenCL" << std::endl;
			ec = btcb::error_cli::generic;
		}
	}
	else if (vm.count ("key_create"))
	{
		btcb::keypair pair;
		std::cout << "Private: " << pair.prv.data.to_string () << std::endl
		          << "Public: " << pair.pub.to_string () << std::endl
		          << "Account: " << pair.pub.to_account () << std::endl;
	}
	else if (vm.count ("key_expand"))
	{
		if (vm.count ("key") == 1)
		{
			btcb::uint256_union prv;
			prv.decode_hex (vm["key"].as<std::string> ());
			btcb::uint256_union pub (btcb::pub_key (prv));
			std::cout << "Private: " << prv.to_string () << std::endl
			          << "Public: " << pub.to_string () << std::endl
			          << "Account: " << pub.to_account () << std::endl;
		}
		else
		{
			std::cerr << "key_expand command requires one <key> option\n";
			ec = btcb::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("wallet_add_adhoc"))
	{
		if (vm.count ("wallet") == 1 && vm.count ("key") == 1)
		{
			btcb::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
			{
				std::string password;
				if (vm.count ("password") > 0)
				{
					password = vm["password"].as<std::string> ();
				}
				inactive_node node (data_path);
				auto wallet (node.node->wallets.open (wallet_id));
				if (wallet != nullptr)
				{
					auto transaction (wallet->wallets.tx_begin_write ());
					if (!wallet->enter_password (transaction, password))
					{
						btcb::raw_key key;
						if (!key.data.decode_hex (vm["key"].as<std::string> ()))
						{
							wallet->store.insert_adhoc (transaction, key);
						}
						else
						{
							std::cerr << "Invalid key\n";
							ec = btcb::error_cli::invalid_arguments;
						}
					}
					else
					{
						std::cerr << "Invalid password\n";
						ec = btcb::error_cli::invalid_arguments;
					}
				}
				else
				{
					std::cerr << "Wallet doesn't exist\n";
					ec = btcb::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id\n";
				ec = btcb::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "wallet_add command requires one <wallet> option and one <key> option and optionally one <password> option\n";
			ec = btcb::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("wallet_change_seed"))
	{
		if (vm.count ("wallet") == 1 && (vm.count ("seed") == 1 || vm.count ("key") == 1))
		{
			btcb::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
			{
				std::string password;
				if (vm.count ("password") > 0)
				{
					password = vm["password"].as<std::string> ();
				}
				inactive_node node (data_path);
				auto wallet (node.node->wallets.open (wallet_id));
				if (wallet != nullptr)
				{
					auto transaction (wallet->wallets.tx_begin_write ());
					if (!wallet->enter_password (transaction, password))
					{
						btcb::raw_key seed;
						if (vm.count ("seed"))
						{
							if (seed.data.decode_hex (vm["seed"].as<std::string> ()))
							{
								std::cerr << "Invalid seed\n";
								ec = btcb::error_cli::invalid_arguments;
							}
						}
						else if (seed.data.decode_hex (vm["key"].as<std::string> ()))
						{
							std::cerr << "Invalid key seed\n";
							ec = btcb::error_cli::invalid_arguments;
						}
						if (!ec)
						{
							std::cout << "Changing seed and caching work. Please wait..." << std::endl;
							wallet->change_seed (transaction, seed);
						}
					}
					else
					{
						std::cerr << "Invalid password\n";
						ec = btcb::error_cli::invalid_arguments;
					}
				}
				else
				{
					std::cerr << "Wallet doesn't exist\n";
					ec = btcb::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id\n";
				ec = btcb::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "wallet_change_seed command requires one <wallet> option and one <seed> option and optionally one <password> option\n";
			ec = btcb::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("wallet_create"))
	{
		btcb::raw_key seed_key;
		if (vm.count ("seed") == 1)
		{
			if (seed_key.data.decode_hex (vm["seed"].as<std::string> ()))
			{
				std::cerr << "Invalid seed\n";
				ec = btcb::error_cli::invalid_arguments;
			}
		}
		else if (vm.count ("seed") > 1)
		{
			std::cerr << "wallet_create command allows one optional <seed> parameter\n";
			ec = btcb::error_cli::invalid_arguments;
		}
		else if (vm.count ("key") == 1)
		{
			if (seed_key.data.decode_hex (vm["key"].as<std::string> ()))
			{
				std::cerr << "Invalid seed key\n";
				ec = btcb::error_cli::invalid_arguments;
			}
		}
		else if (vm.count ("key") > 1)
		{
			std::cerr << "wallet_create command allows one optional <key> seed parameter\n";
			ec = btcb::error_cli::invalid_arguments;
		}
		if (!ec)
		{
			inactive_node node (data_path);
			btcb::keypair wallet_key;
			auto wallet (node.node->wallets.create (wallet_key.pub));
			if (wallet != nullptr)
			{
				if (vm.count ("password") > 0)
				{
					std::string password (vm["password"].as<std::string> ());
					auto transaction (wallet->wallets.tx_begin_write ());
					auto error (wallet->store.rekey (transaction, password));
					if (error)
					{
						std::cerr << "Password change error\n";
						ec = btcb::error_cli::invalid_arguments;
					}
				}
				if (vm.count ("seed") || vm.count ("key"))
				{
					auto transaction (wallet->wallets.tx_begin_write ());
					wallet->change_seed (transaction, seed_key);
				}
				std::cout << wallet_key.pub.to_string () << std::endl;
			}
			else
			{
				std::cerr << "Wallet creation error\n";
				ec = btcb::error_cli::invalid_arguments;
			}
		}
	}
	else if (vm.count ("wallet_decrypt_unsafe"))
	{
		if (vm.count ("wallet") == 1)
		{
			std::string password;
			if (vm.count ("password") == 1)
			{
				password = vm["password"].as<std::string> ();
			}
			btcb::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
			{
				inactive_node node (data_path);
				auto existing (node.node->wallets.items.find (wallet_id));
				if (existing != node.node->wallets.items.end ())
				{
					auto transaction (existing->second->wallets.tx_begin_write ());
					if (!existing->second->enter_password (transaction, password))
					{
						btcb::raw_key seed;
						existing->second->store.seed (seed, transaction);
						std::cout << boost::str (boost::format ("Seed: %1%\n") % seed.data.to_string ());
						for (auto i (existing->second->store.begin (transaction)), m (existing->second->store.end ()); i != m; ++i)
						{
							btcb::account account (i->first);
							btcb::raw_key key;
							auto error (existing->second->store.fetch (transaction, account, key));
							assert (!error);
							std::cout << boost::str (boost::format ("Pub: %1% Prv: %2%\n") % account.to_account () % key.data.to_string ());
							if (btcb::pub_key (key.data) != account)
							{
								std::cerr << boost::str (boost::format ("Invalid private key %1%\n") % key.data.to_string ());
							}
						}
					}
					else
					{
						std::cerr << "Invalid password\n";
						ec = btcb::error_cli::invalid_arguments;
					}
				}
				else
				{
					std::cerr << "Wallet doesn't exist\n";
					ec = btcb::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id\n";
				ec = btcb::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "wallet_decrypt_unsafe requires one <wallet> option\n";
			ec = btcb::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("wallet_destroy"))
	{
		if (vm.count ("wallet") == 1)
		{
			btcb::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
			{
				inactive_node node (data_path);
				if (node.node->wallets.items.find (wallet_id) != node.node->wallets.items.end ())
				{
					node.node->wallets.destroy (wallet_id);
				}
				else
				{
					std::cerr << "Wallet doesn't exist\n";
					ec = btcb::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id\n";
				ec = btcb::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "wallet_destroy requires one <wallet> option\n";
			ec = btcb::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("wallet_import"))
	{
		if (vm.count ("file") == 1)
		{
			bool forced (false);
			std::string filename (vm["file"].as<std::string> ());
			std::ifstream stream;
			stream.open (filename.c_str ());
			if (!stream.fail ())
			{
				std::stringstream contents;
				contents << stream.rdbuf ();
				std::string password;
				if (vm.count ("password") == 1)
				{
					password = vm["password"].as<std::string> ();
				}
				if (vm.count ("force") == 1)
				{
					forced = vm["force"].as<bool> ();
				}
				if (vm.count ("wallet") == 1)
				{
					btcb::uint256_union wallet_id;
					if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
					{
						inactive_node node (data_path);
						auto existing (node.node->wallets.items.find (wallet_id));
						if (existing != node.node->wallets.items.end ())
						{
							bool valid (false);
							{
								auto transaction (node.node->wallets.tx_begin_write ());
								valid = existing->second->store.valid_password (transaction);
								if (!valid)
								{
									valid = !existing->second->enter_password (transaction, password);
								}
							}
							if (valid)
							{
								if (existing->second->import (contents.str (), password))
								{
									std::cerr << "Unable to import wallet\n";
									ec = btcb::error_cli::invalid_arguments;
								}
								else
								{
									std::cout << "Import completed\n";
								}
							}
							else
							{
								std::cerr << boost::str (boost::format ("Invalid password for wallet %1%\nNew wallet should have empty (default) password or passwords for new wallet & json file should match\n") % wallet_id.to_string ());
								ec = btcb::error_cli::invalid_arguments;
							}
						}
						else
						{
							if (!forced)
							{
								std::cerr << "Wallet doesn't exist\n";
								ec = btcb::error_cli::invalid_arguments;
							}
							else
							{
								bool error (true);
								{
									std::lock_guard<std::mutex> lock (node.node->wallets.mutex);
									auto transaction (node.node->wallets.tx_begin_write ());
									btcb::wallet wallet (error, transaction, node.node->wallets, wallet_id.to_string (), contents.str ());
								}
								if (error)
								{
									std::cerr << "Unable to import wallet\n";
									ec = btcb::error_cli::invalid_arguments;
								}
								else
								{
									node.node->wallets.reload ();
									std::lock_guard<std::mutex> lock (node.node->wallets.mutex);
									release_assert (node.node->wallets.items.find (wallet_id) != node.node->wallets.items.end ());
									std::cout << "Import completed\n";
								}
							}
						}
					}
					else
					{
						std::cerr << "Invalid wallet id\n";
						ec = btcb::error_cli::invalid_arguments;
					}
				}
				else
				{
					std::cerr << "wallet_import requires one <wallet> option\n";
					ec = btcb::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "Unable to open <file>\n";
				ec = btcb::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "wallet_import requires one <file> option\n";
			ec = btcb::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("wallet_list"))
	{
		inactive_node node (data_path);
		for (auto i (node.node->wallets.items.begin ()), n (node.node->wallets.items.end ()); i != n; ++i)
		{
			std::cout << boost::str (boost::format ("Wallet ID: %1%\n") % i->first.to_string ());
			auto transaction (i->second->wallets.tx_begin_read ());
			for (auto j (i->second->store.begin (transaction)), m (i->second->store.end ()); j != m; ++j)
			{
				std::cout << btcb::uint256_union (j->first).to_account () << '\n';
			}
		}
	}
	else if (vm.count ("wallet_remove"))
	{
		if (vm.count ("wallet") == 1 && vm.count ("account") == 1)
		{
			inactive_node node (data_path);
			btcb::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
			{
				auto wallet (node.node->wallets.items.find (wallet_id));
				if (wallet != node.node->wallets.items.end ())
				{
					btcb::account account_id;
					if (!account_id.decode_account (vm["account"].as<std::string> ()))
					{
						auto transaction (wallet->second->wallets.tx_begin_write ());
						auto account (wallet->second->store.find (transaction, account_id));
						if (account != wallet->second->store.end ())
						{
							wallet->second->store.erase (transaction, account_id);
						}
						else
						{
							std::cerr << "Account not found in wallet\n";
							ec = btcb::error_cli::invalid_arguments;
						}
					}
					else
					{
						std::cerr << "Invalid account id\n";
						ec = btcb::error_cli::invalid_arguments;
					}
				}
				else
				{
					std::cerr << "Wallet not found\n";
					ec = btcb::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id\n";
				ec = btcb::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "wallet_remove command requires one <wallet> and one <account> option\n";
			ec = btcb::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("wallet_representative_get"))
	{
		if (vm.count ("wallet") == 1)
		{
			btcb::uint256_union wallet_id;
			if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
			{
				inactive_node node (data_path);
				auto wallet (node.node->wallets.items.find (wallet_id));
				if (wallet != node.node->wallets.items.end ())
				{
					auto transaction (wallet->second->wallets.tx_begin_read ());
					auto representative (wallet->second->store.representative (transaction));
					std::cout << boost::str (boost::format ("Representative: %1%\n") % representative.to_account ());
				}
				else
				{
					std::cerr << "Wallet not found\n";
					ec = btcb::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "Invalid wallet id\n";
				ec = btcb::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "wallet_representative_get requires one <wallet> option\n";
			ec = btcb::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("wallet_representative_set"))
	{
		if (vm.count ("wallet") == 1)
		{
			if (vm.count ("account") == 1)
			{
				btcb::uint256_union wallet_id;
				if (!wallet_id.decode_hex (vm["wallet"].as<std::string> ()))
				{
					btcb::account account;
					if (!account.decode_account (vm["account"].as<std::string> ()))
					{
						inactive_node node (data_path);
						auto wallet (node.node->wallets.items.find (wallet_id));
						if (wallet != node.node->wallets.items.end ())
						{
							auto transaction (wallet->second->wallets.tx_begin_write ());
							wallet->second->store.representative_set (transaction, account);
						}
						else
						{
							std::cerr << "Wallet not found\n";
							ec = btcb::error_cli::invalid_arguments;
						}
					}
					else
					{
						std::cerr << "Invalid account\n";
						ec = btcb::error_cli::invalid_arguments;
					}
				}
				else
				{
					std::cerr << "Invalid wallet id\n";
					ec = btcb::error_cli::invalid_arguments;
				}
			}
			else
			{
				std::cerr << "wallet_representative_set requires one <account> option\n";
				ec = btcb::error_cli::invalid_arguments;
			}
		}
		else
		{
			std::cerr << "wallet_representative_set requires one <wallet> option\n";
			ec = btcb::error_cli::invalid_arguments;
		}
	}
	else if (vm.count ("vote_dump") == 1)
	{
		inactive_node node (data_path);
		auto transaction (node.node->store.tx_begin_read ());
		for (auto i (node.node->store.vote_begin (transaction)), n (node.node->store.vote_end ()); i != n; ++i)
		{
			auto vote (i->second);
			std::cerr << boost::str (boost::format ("%1%\n") % vote->to_json ());
		}
	}
	else
	{
		ec = btcb::error_cli::unknown_command;
	}

	return ec;
}

namespace
{
void reset_confirmation_heights (btcb::block_store & store)
{
	// First do a clean sweep
	auto transaction (store.tx_begin_write ());
	store.confirmation_height_clear (transaction);

	// Then make sure the confirmation height of the genesis account open block is 1
	btcb::network_params network_params;
	auto const & genesis_account = network_params.ledger.genesis_account;
	btcb::account_info account_info;
	auto error = store.account_get (transaction, genesis_account, account_info);
	release_assert (!error);
	account_info.confirmation_height = 1;
	store.account_put (transaction, genesis_account, account_info);
}
}
