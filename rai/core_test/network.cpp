#include <gtest/gtest.h>
#include <boost/thread.hpp>
#include <rai/core/core.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

TEST (network, tcp_connection)
{
    boost::asio::io_service service;
    boost::asio::ip::tcp::acceptor acceptor (service);
    boost::asio::ip::tcp::endpoint endpoint (boost::asio::ip::address_v4::any (), 24000);
    acceptor.open (endpoint.protocol ());
    acceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));
    acceptor.bind (endpoint);
    acceptor.listen ();
    boost::asio::ip::tcp::socket incoming (service);
    auto done1 (false);
    std::string message1;
    acceptor.async_accept (incoming, 
       [&done1, &message1] (boost::system::error_code const & ec_a)
       {
           if (ec_a)
           {
               message1 = ec_a.message ();
               std::cerr << message1;
           }
           done1 = true;}
       );
    boost::asio::ip::tcp::socket connector (service);
    auto done2 (false);
    std::string message2;
    connector.async_connect (boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4::loopback (), 24000), 
        [&done2, &message2] (boost::system::error_code const & ec_a)
        {
            if (ec_a)
            {
                message2 = ec_a.message ();
                std::cerr << message2;
            }
            done2 = true;
        });
    while (!done1 || !done2)
    {
        service.poll_one ();
    }
    ASSERT_EQ (0, message1.size ());
    ASSERT_EQ (0, message2.size ());
}

TEST (network, construction)
{
    rai::system system (24000, 1);
    ASSERT_EQ (1, system.clients.size ());
    ASSERT_EQ (24000, system.clients [0]->network.socket.local_endpoint ().port ());
}

TEST (network, self_discard)
{
    rai::system system (24000, 1);
	system.clients [0]->network.remote = system.clients [0]->network.endpoint ();
	ASSERT_EQ (0, system.clients [0]->network.bad_sender_count);
	system.clients [0]->network.receive_action (boost::system::error_code {}, 0);
	ASSERT_EQ (1, system.clients [0]->network.bad_sender_count);
}

TEST (network, send_keepalive)
{
    rai::system system (24000, 1);
    auto list1 (system.clients [0]->peers.list ());
    ASSERT_EQ (0, list1.size ());
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor));
    client1->start ();
    system.clients [0]->network.send_keepalive (client1->network.endpoint ());
    auto initial (system.clients [0]->network.parser.keepalive_count);
    ASSERT_EQ (0, system.clients [0]->peers.list ().size ());
    ASSERT_EQ (0, client1->peers.list ().size ());
    auto iterations (0);
    while (system.clients [0]->network.parser.keepalive_count == initial)
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    auto peers1 (system.clients [0]->peers.list ());
    auto peers2 (client1->peers.list ());
    ASSERT_EQ (1, peers1.size ());
    ASSERT_EQ (1, peers2.size ());
    ASSERT_NE (peers1.end (), std::find_if (peers1.begin (), peers1.end (), [&client1] (rai::peer_information const & information_a) {return information_a.endpoint == client1->network.endpoint ();}));
    ASSERT_NE (peers2.end (), std::find_if (peers2.begin (), peers2.end (), [&system] (rai::peer_information const & information_a) {return information_a.endpoint == system.clients [0]->network.endpoint ();}));
    client1->stop ();
}

TEST (network, keepalive_ipv4)
{
    rai::system system (24000, 1);
    auto list1 (system.clients [0]->peers.list ());
    ASSERT_EQ (0, list1.size ());
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor));
    client1->start ();
    client1->send_keepalive (rai::endpoint (boost::asio::ip::address_v4::loopback (), 24000));
    auto initial (system.clients [0]->network.parser.keepalive_count);
    auto iterations (0);
    while (system.clients [0]->network.parser.keepalive_count == initial)
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    client1->stop ();
}

TEST (network, multi_keepalive)
{
    rai::system system (24000, 1);
    auto list1 (system.clients [0]->peers.list ());
    ASSERT_EQ (0, list1.size ());
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor));
    ASSERT_FALSE (init1.error ());
    client1->start ();
    ASSERT_EQ (0, client1->peers.size ());
    client1->network.send_keepalive (system.clients [0]->network.endpoint ());
    ASSERT_EQ (0, client1->peers.size ());
    ASSERT_EQ (0, system.clients [0]->peers.size ());
    auto iterations1 (0);
    while (system.clients [0]->peers.size () != 1)
    {
        system.service->poll_one ();
        ++iterations1;
        ASSERT_LT (iterations1, 200);
    }
    rai::client_init init2;
    auto client2 (std::make_shared <rai::client> (init2, system.service, 24002, system.processor));
    ASSERT_FALSE (init2.error ());
    client2->start ();
    client2->network.send_keepalive (system.clients [0]->network.endpoint ());
    auto iterations2 (0);
    while (client1->peers.size () != 2 || system.clients [0]->peers.size () != 2 || client2->peers.size () != 2)
    {
        system.service->poll_one ();
        ++iterations2;
        ASSERT_LT (iterations2, 200);
    }
    client1->stop ();
    client2->stop ();
}

TEST (network, send_discarded_publish)
{
    rai::system system (24000, 2);
    std::unique_ptr <rai::send_block> block (new rai::send_block);
    block->work = system.clients [0]->ledger.create_work (*block);
    system.clients [0]->network.republish_block (std::move (block));
    rai::genesis genesis;
    ASSERT_EQ (genesis.hash (), system.clients [0]->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_EQ (genesis.hash (), system.clients [1]->ledger.latest (rai::test_genesis_key.pub));
    auto iterations (0);
    while (system.clients [1]->network.parser.publish_count == 0)
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    ASSERT_EQ (genesis.hash (), system.clients [0]->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_EQ (genesis.hash (), system.clients [1]->ledger.latest (rai::test_genesis_key.pub));
}

TEST (network, send_invalid_publish)
{
    rai::system system (24000, 2);
    std::unique_ptr <rai::send_block> block (new rai::send_block);
    block->hashables.previous.clear ();
    block->hashables.balance = 20;
    block->work = system.clients [0]->ledger.create_work (*block);
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block->hash (), block->signature);
    system.clients [0]->network.republish_block (std::move (block));
    rai::genesis genesis;
    ASSERT_EQ (genesis.hash (), system.clients [0]->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_EQ (genesis.hash (), system.clients [1]->ledger.latest (rai::test_genesis_key.pub));
    auto iterations (0);
    while (system.clients [1]->network.parser.publish_count == 0)
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    ASSERT_EQ (genesis.hash (), system.clients [0]->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_EQ (genesis.hash (), system.clients [1]->ledger.latest (rai::test_genesis_key.pub));
}

TEST (network, send_valid_confirm_ack)
{
    rai::system system (24000, 2);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::keypair key2;
    system.wallet (1)->store.insert (key2.prv);
    rai::send_block block2;
    rai::frontier frontier1;
    ASSERT_FALSE (system.clients [0]->store.latest_get (rai::test_genesis_key.pub, frontier1));
    block2.hashables.previous = frontier1.hash;
    block2.hashables.balance = 50;
    block2.hashables.destination = key2.pub;
    block2.work = system.clients [0]->ledger.create_work (block2);
    auto hash2 (block2.hash ());
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, hash2, block2.signature);
    rai::frontier frontier2;
    ASSERT_FALSE (system.clients [1]->store.latest_get (rai::test_genesis_key.pub, frontier2));
    system.clients [0]->processor.process_receive_republish (std::unique_ptr <rai::block> (new rai::send_block (block2)));
    auto iterations (0);
    while (system.clients [1]->network.parser.confirm_ack_count == 0)
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    rai::frontier frontier3;
    ASSERT_FALSE (system.clients [1]->store.latest_get (rai::test_genesis_key.pub, frontier3));
    ASSERT_FALSE (frontier2.hash == frontier3.hash);
    ASSERT_EQ (hash2, frontier3.hash);
    ASSERT_EQ (50, system.clients [1]->ledger.account_balance (rai::test_genesis_key.pub));
}

TEST (network, send_valid_publish)
{
    rai::system system (24000, 2);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::keypair key2;
    system.wallet (1)->store.insert (key2.prv);
    rai::send_block block2;
    rai::frontier frontier1;
    ASSERT_FALSE (system.clients [0]->store.latest_get (rai::test_genesis_key.pub, frontier1));
    block2.hashables.previous = frontier1.hash;
    block2.hashables.balance = 50;
    block2.hashables.destination = key2.pub;
    block2.work = system.clients [0]->ledger.create_work (block2);
    auto hash2 (block2.hash ());
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, hash2, block2.signature);
    rai::frontier frontier2;
    ASSERT_FALSE (system.clients [1]->store.latest_get (rai::test_genesis_key.pub, frontier2));
    system.clients [1]->processor.process_receive_republish (std::unique_ptr <rai::block> (new rai::send_block (block2)));
    auto iterations (0);
    while (system.clients [0]->network.parser.publish_count == 0)
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    rai::frontier frontier3;
    ASSERT_FALSE (system.clients [1]->store.latest_get (rai::test_genesis_key.pub, frontier3));
    ASSERT_FALSE (frontier2.hash == frontier3.hash);
    ASSERT_EQ (hash2, frontier3.hash);
    ASSERT_EQ (50, system.clients [1]->ledger.account_balance (rai::test_genesis_key.pub));
}

TEST (network, send_insufficient_work)
{
    rai::system system (24000, 2);
    std::unique_ptr <rai::send_block> block (new rai::send_block);
    block->hashables.previous.clear ();
    block->hashables.balance = 20;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block->hash (), block->signature);
    rai::publish publish (std::move (block));
    std::shared_ptr <std::vector <uint8_t>> bytes (new std::vector <uint8_t>);
    {
        rai::vectorstream stream (*bytes);
        publish.serialize (stream);
    }
    auto client (system.clients [1]->shared ());
    system.clients [0]->network.send_buffer (bytes->data (), bytes->size (), system.clients [1]->network.endpoint (), [bytes, client] (boost::system::error_code const & ec, size_t size) {});
    ASSERT_EQ (0, system.clients [0]->network.parser.insufficient_work_count);
    auto iterations (0);
    while (system.clients [1]->network.parser.insufficient_work_count == 0)
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    ASSERT_EQ (1, system.clients [1]->network.parser.insufficient_work_count);
}

TEST (receivable_processor, confirm_insufficient_pos)
{
    rai::system system (24000, 1);
    auto & client1 (*system.clients [0]);
    rai::genesis genesis;
    rai::send_block block1;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance.clear ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    ASSERT_EQ (rai::process_result::progress, client1.ledger.process (block1));
    client1.conflicts.start (block1, true);
    rai::keypair key1;
    rai::confirm_ack con1;
    con1.vote.account = key1.pub;
    con1.vote.block = block1.clone ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, con1.vote.hash (), con1.vote.signature);
	client1.processor.process_message (con1, rai::endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000));
}

TEST (receivable_processor, confirm_sufficient_pos)
{
    rai::system system (24000, 1);
    auto & client1 (*system.clients [0]);
    rai::genesis genesis;
    rai::send_block block1;
    block1.hashables.previous = genesis.hash ();
    block1.hashables.balance.clear ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1.hash (), block1.signature);
    ASSERT_EQ (rai::process_result::progress, client1.ledger.process (block1));
    client1.conflicts.start (block1, true);
    rai::keypair key1;
    rai::confirm_ack con1;
    con1.vote.account = key1.pub;
    con1.vote.block = block1.clone ();
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, con1.vote.hash (), con1.vote.signature);
	client1.processor.process_message (con1, rai::endpoint (boost::asio::ip::address_v4 (0x7f000001), 10000));
}

TEST (receivable_processor, send_with_receive)
{
    auto amount (std::numeric_limits <rai::uint128_t>::max ());
    rai::system system (24000, 2);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::keypair key2;
    system.wallet (1)->store.insert (key2.prv);
    auto block1 (new rai::send_block ());
    rai::frontier frontier1;
    ASSERT_FALSE (system.clients [0]->ledger.store.latest_get (rai::test_genesis_key.pub, frontier1));
    block1->hashables.previous = frontier1.hash;
    block1->hashables.balance = amount - 100;
    block1->hashables.destination = key2.pub;
    block1->work = system.clients [0]->ledger.create_work (*block1);
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, block1->hash (), block1->signature);
    ASSERT_EQ (amount, system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_EQ (0, system.clients [0]->ledger.account_balance (key2.pub));
    ASSERT_EQ (amount, system.clients [1]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_EQ (0, system.clients [1]->ledger.account_balance (key2.pub));
    system.clients [0]->processor.process_receive_republish (block1->clone ());
    system.clients [1]->processor.process_receive_republish (block1->clone ());
    ASSERT_EQ (amount - 100, system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_EQ (0, system.clients [0]->ledger.account_balance (key2.pub));
    ASSERT_EQ (amount - 100, system.clients [1]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_EQ (0, system.clients [1]->ledger.account_balance (key2.pub));
    auto iterations (0);
    while (system.clients [0]->ledger.account_balance (key2.pub) != 100)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    ASSERT_EQ (amount - 100, system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_EQ (100, system.clients [0]->ledger.account_balance (key2.pub));
    ASSERT_EQ (amount - 100, system.clients [1]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_EQ (100, system.clients [1]->ledger.account_balance (key2.pub));
}

TEST (rpc, account_create)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.clients [0], true);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "wallet_create");
    std::string wallet;
    system.clients [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    auto account_text (response_tree.get <std::string> ("account"));
    rai::uint256_union account;
    ASSERT_FALSE (account.decode_base58check (account_text));
    ASSERT_NE (system.wallet (0)->store.end (), system.wallet (0)->store.find (account));
}

TEST (rpc, account_balance_exact)
{
	rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "account_balance_exact");
    request_tree.put ("account", account);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string balance_text (response_tree.get <std::string> ("balance"));
    ASSERT_EQ ("340282366920938463463374607431768211455", balance_text);
}

TEST (rpc, account_balance)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "account_balance");
    request_tree.put ("account", account);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string balance_text (response_tree.get <std::string> ("balance"));
    ASSERT_EQ ("3402823669209384634", balance_text);
}

TEST (rpc, account_weight_exact)
{
    rai::keypair key;
    rai::system system (24000, 1);
    rai::frontier frontier;
    ASSERT_FALSE (system.clients [0]->store.latest_get (rai::test_genesis_key.pub, frontier));
    rai::change_block block (key.pub, frontier.hash, rai::test_genesis_key.prv, rai::test_genesis_key.pub);
    block.work = system.clients [0]->ledger.create_work (block);
    ASSERT_EQ (rai::process_result::progress, system.clients [0]->ledger.process (block));
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.clients [0], true);
    std::string account;
    key.pub.encode_base58check (account);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "account_weight_exact");
    request_tree.put ("account", account);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string balance_text (response_tree.get <std::string> ("weight"));
    ASSERT_EQ ("340282366920938463463374607431768211455", balance_text);
}

TEST (rpc, account_weight)
{
    rai::keypair key;
    rai::system system (24000, 1);
    rai::frontier frontier;
    ASSERT_FALSE (system.clients [0]->store.latest_get (rai::test_genesis_key.pub, frontier));
    rai::change_block block (key.pub, frontier.hash, rai::test_genesis_key.prv, rai::test_genesis_key.pub);
    block.work = system.clients [0]->ledger.create_work (block);
    ASSERT_EQ (rai::process_result::progress, system.clients [0]->ledger.process (block));
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.clients [0], true);
    std::string account;
    key.pub.encode_base58check (account);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "account_weight");
    request_tree.put ("account", account);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string balance_text (response_tree.get <std::string> ("weight"));
    ASSERT_EQ ("3402823669209384634", balance_text);
}

TEST (rpc, wallet_contains)
{
	rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.clients [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "wallet_contains");
    request_tree.put ("account", account);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string exists_text (response_tree.get <std::string> ("exists"));
    ASSERT_EQ ("1", exists_text);
}

TEST (rpc, wallet_doesnt_contain)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.clients [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "wallet_contains");
    request_tree.put ("account", account);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string exists_text (response_tree.get <std::string> ("exists"));
    ASSERT_EQ ("0", exists_text);
}

TEST (rpc, validate_account)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "validate_account");
    request_tree.put ("account", account);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string exists_text (response_tree.get <std::string> ("valid"));
    ASSERT_EQ ("1", exists_text);
}

TEST (rpc, validate_account_invalid)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    account [0] ^= 0x1;
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    request_tree.put ("action", "validate_account");
    request_tree.put ("account", account);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string exists_text (response_tree.get <std::string> ("valid"));
    ASSERT_EQ ("0", exists_text);
}

TEST (rpc, send_exact)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::keypair key1;
    system.wallet (0)->store.insert (key1.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.clients [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "send_exact");
    request_tree.put ("account", account);
    request_tree.put ("amount", "100");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string sent_text (response_tree.get <std::string> ("sent"));
    ASSERT_EQ ("1", sent_text);
}

TEST (rpc, send)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::keypair key1;
    system.wallet (0)->store.insert (key1.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.clients [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "send");
    request_tree.put ("account", account);
    request_tree.put ("amount", "1");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    auto balance1 (system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
    rpc (request, response);
    ASSERT_EQ (balance1 - rai::scale_64bit_base10, system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string sent_text (response_tree.get <std::string> ("sent"));
    ASSERT_EQ ("1", sent_text);
}

TEST (rpc, send_fail)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    rai::keypair key1;
    system.wallet (0)->store.insert (key1.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.clients [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "send");
    request_tree.put ("account", account);
    request_tree.put ("amount", "100");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string sent_text (response_tree.get <std::string> ("sent"));
    ASSERT_EQ ("0", sent_text);
}

TEST (rpc, wallet_add)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.clients [0], true);
    rai::keypair key1;
    std::string key_text;
    key1.prv.encode_hex (key_text);
    system.wallet (0)->store.insert (key1.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.clients [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "wallet_add");
    request_tree.put ("key", key_text);
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string account_text1 (response_tree.get <std::string> ("account"));
    std::string account_text2;
    key1.pub.encode_base58check (account_text2);
    ASSERT_EQ (account_text1, account_text2);
}

TEST (rpc, wallet_password_valid)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.clients [0], true);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.clients [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "password_valid");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string account_text1 (response_tree.get <std::string> ("valid"));
    ASSERT_EQ (account_text1, "1");
}

TEST (rpc, wallet_password_change)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.clients [0], true);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.clients [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "password_change");
    request_tree.put ("password", "test");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string account_text1 (response_tree.get <std::string> ("changed"));
    ASSERT_EQ (account_text1, "1");
    ASSERT_TRUE (system.wallet (0)->store.valid_password ());
    system.wallet (0)->store.enter_password ("");
    ASSERT_FALSE (system.wallet (0)->store.valid_password ());
    system.wallet (0)->store.enter_password ("test");
    ASSERT_TRUE (system.wallet (0)->store.valid_password ());
}

TEST (rpc, wallet_password_enter)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.clients [0], true);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.clients [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "password_enter");
    request_tree.put ("password", "");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string account_text1 (response_tree.get <std::string> ("valid"));
    ASSERT_EQ (account_text1, "1");
}

TEST (network, receive_weight_change)
{
    rai::system system (24000, 2);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::keypair key2;
    system.wallet (1)->store.insert (key2.prv);
    system.wallet (1)->store.representative_set (key2.pub);
    ASSERT_FALSE (system.wallet (0)->send (key2.pub, 2));
	auto iterations (0);
    while (std::any_of (system.clients.begin (), system.clients.end (), [&] (std::shared_ptr <rai::client> const & client_a) {return client_a->ledger.weight (key2.pub) != 2;}))
    {
        system.service->poll_one ();
        system.processor.poll_one ();
		++iterations;
		ASSERT_LT (iterations, 200);
    }
}

TEST (rpc, wallet_list)
{
	rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::keypair key2;
    system.wallet (0)->store.insert (key2.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.clients [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "wallet_list");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    auto & accounts_node (response_tree.get_child ("accounts"));
    std::vector <rai::uint256_union> accounts;
    for (auto i (accounts_node.begin ()), j (accounts_node.end ()); i != j; ++i)
    {
        auto account (i->second.get <std::string> (""));
        rai::uint256_union number;
        ASSERT_FALSE (number.decode_base58check (account));
        accounts.push_back (number);
    }
    ASSERT_EQ (2, accounts.size ());
    for (auto i (accounts.begin ()), j (accounts.end ()); i != j; ++i)
    {
        ASSERT_NE (system.wallet (0)->store.end (), system.wallet (0)->store.find (*i));
    }
}

TEST (rpc, wallet_key_valid)
{
    rai::system system (24000, 1);
    auto pool (boost::make_shared <boost::network::utils::thread_pool> ());
    rai::rpc rpc (system.service, pool, boost::asio::ip::address_v6::loopback (), 25000, *system.clients [0], true);
    std::string account;
    rai::test_genesis_key.pub.encode_base58check (account);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    boost::network::http::server <rai::rpc>::request request;
    boost::network::http::server <rai::rpc>::response response;
    request.method = "POST";
    boost::property_tree::ptree request_tree;
    std::string wallet;
    system.clients [0]->wallets.items.begin ()->first.encode_hex (wallet);
    request_tree.put ("wallet", wallet);
    request_tree.put ("action", "wallet_key_valid");
    std::stringstream ostream;
    boost::property_tree::write_json (ostream, request_tree);
    request.body = ostream.str ();
    rpc (request, response);
    ASSERT_EQ (boost::network::http::server <rai::rpc>::response::ok, response.status);
    boost::property_tree::ptree response_tree;
    std::stringstream istream (response.content);
    boost::property_tree::read_json (istream, response_tree);
    std::string exists_text (response_tree.get <std::string> ("valid"));
    ASSERT_EQ ("1", exists_text);
}

TEST (parse_endpoint, valid)
{
    std::string string ("127.0.0.1:24000");
    rai::endpoint endpoint;
    ASSERT_FALSE (rai::parse_endpoint (string, endpoint));
    ASSERT_EQ (boost::asio::ip::address_v4::loopback (), endpoint.address ());
    ASSERT_EQ (24000, endpoint.port ());
}

TEST (parse_endpoint, invalid_port)
{
    std::string string ("127.0.0.1:24a00");
    rai::endpoint endpoint;
    ASSERT_TRUE (rai::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, invalid_address)
{
    std::string string ("127.0q.0.1:24000");
    rai::endpoint endpoint;
    ASSERT_TRUE (rai::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, nothing)
{
    std::string string ("127.0q.0.1:24000");
    rai::endpoint endpoint;
    ASSERT_TRUE (rai::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_address)
{
    std::string string (":24000");
    rai::endpoint endpoint;
    ASSERT_TRUE (rai::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_port)
{
    std::string string ("127.0.0.1:");
    rai::endpoint endpoint;
    ASSERT_TRUE (rai::parse_endpoint (string, endpoint));
}

TEST (parse_endpoint, no_colon)
{
    std::string string ("127.0.0.1");
    rai::endpoint endpoint;
    ASSERT_TRUE (rai::parse_endpoint (string, endpoint));
}

TEST (bulk_pull, no_address)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.clients [0]));
    std::unique_ptr <rai::bulk_pull> req (new rai::bulk_pull);
    req->start = 1;
    req->end = 2;
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::bulk_pull_server> (connection, std::move (req)));
    ASSERT_EQ (request->current, request->request->end);
    ASSERT_FALSE (request->current.is_zero ());
}

TEST (bulk_pull, genesis_to_end)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.clients [0]));
    std::unique_ptr <rai::bulk_pull> req (new rai::bulk_pull {});
    req->start = rai::test_genesis_key.pub;
    req->end.clear ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::bulk_pull_server> (connection, std::move (req)));
    ASSERT_EQ (system.clients [0]->ledger.latest (rai::test_genesis_key.pub), request->current);
    ASSERT_EQ (request->request->end, request->request->end);
}

TEST (bulk_pull, no_end)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.clients [0]));
    std::unique_ptr <rai::bulk_pull> req (new rai::bulk_pull {});
    req->start = rai::test_genesis_key.pub;
    req->end = 1;
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::bulk_pull_server> (connection, std::move (req)));
    ASSERT_EQ (request->current, request->request->end);
    ASSERT_FALSE (request->current.is_zero ());
}

TEST (bulk_pull, end_not_owned)
{
    rai::system system (24000, 1);
    rai::keypair key2;
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    ASSERT_FALSE (system.wallet (0)->send (key2.pub, 100));
    rai::open_block open;
    open.hashables.representative = key2.pub;
    open.hashables.source = system.clients [0]->ledger.latest (rai::test_genesis_key.pub);
    rai::sign_message (key2.prv, key2.pub, open.hash (), open.signature);
    ASSERT_EQ (rai::process_result::progress, system.clients [0]->ledger.process (open));
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.clients [0]));
    rai::genesis genesis;
    std::unique_ptr <rai::bulk_pull> req (new rai::bulk_pull {});
    req->start = key2.pub;
    req->end = genesis.hash ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::bulk_pull_server> (connection, std::move (req)));
    ASSERT_EQ (request->current, request->request->end);
}

TEST (bulk_pull, none)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.clients [0]));
    rai::genesis genesis;
    std::unique_ptr <rai::bulk_pull> req (new rai::bulk_pull {});
    req->start = genesis.hash ();
    req->end = genesis.hash ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::bulk_pull_server> (connection, std::move (req)));
    auto block (request->get_next ());
    ASSERT_EQ (nullptr, block);
}

TEST (bulk_pull, get_next_on_open)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.clients [0]));
    std::unique_ptr <rai::bulk_pull> req (new rai::bulk_pull {});
    req->start = rai::test_genesis_key.pub;
    req->end.clear ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::bulk_pull_server> (connection, std::move (req)));
    auto block (request->get_next ());
    ASSERT_NE (nullptr, block);
    ASSERT_TRUE (block->previous ().is_zero ());
    ASSERT_FALSE (connection->requests.empty ());
    ASSERT_FALSE (request->current.is_zero ());
    ASSERT_EQ (request->current, request->request->end);
}

TEST (bootstrap_processor, DISABLED_process_none)
{
    rai::system system (24000, 1);
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor));
    ASSERT_FALSE (init1.error ());
    auto done (false);
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint ());
    while (!done)
    {
        system.service->run_one ();
    }
    client1->stop ();
}

TEST (bootstrap_processor, DISABLED_process_incomplete)
{
    rai::system system (24000, 1);
    auto client (std::make_shared <rai::bootstrap_client> (system.clients [0]));
    rai::genesis genesis;
    auto frontier_req_client (std::make_shared <rai::frontier_req_client> (client));
    frontier_req_client->pulls [rai::test_genesis_key.pub] = genesis.hash ();
    auto bulk_pull_client (std::make_shared <rai::bulk_pull_client> (frontier_req_client));
    std::unique_ptr <rai::send_block> block1 (new rai::send_block);
    bulk_pull_client->process_end ();
}

TEST (bootstrap_processor, process_one)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    ASSERT_FALSE (system.wallet (0)->send (rai::test_genesis_key.pub, 100));
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor));
    auto hash1 (system.clients [0]->ledger.latest (rai::test_genesis_key.pub));
    auto hash2 (client1->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_NE (hash1, hash2);
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint ());
    auto iterations (0);
    while (client1->ledger.latest (rai::test_genesis_key.pub) != hash1)
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    auto hash3 (client1->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_EQ (hash1, hash3);
    client1->stop ();
}

TEST (bootstrap_processor, process_two)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    auto hash1 (system.clients [0]->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_FALSE (system.wallet (0)->send (rai::test_genesis_key.pub, 50));
    auto hash2 (system.clients [0]->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_FALSE (system.wallet (0)->send (rai::test_genesis_key.pub, 50));
    auto hash3 (system.clients [0]->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_NE (hash1, hash2);
    ASSERT_NE (hash1, hash3);
    ASSERT_NE (hash2, hash3);
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor));
    ASSERT_FALSE (init1.error ());
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint ());
    auto iterations (0);
    while (client1->ledger.latest (rai::test_genesis_key.pub) != hash3)
    {
        system.service->run_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    auto hash4 (client1->ledger.latest (rai::test_genesis_key.pub));
    ASSERT_EQ (hash3, hash4);
    client1->stop ();
}

TEST (bootstrap_processor, process_new)
{
    rai::system system (24000, 2);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::keypair key2;
    system.wallet (1)->store.insert (key2.prv);
    ASSERT_FALSE (system.wallet (0)->send (key2.pub, 100));
    auto iterations1 (0);
    while (system.clients [0]->ledger.account_balance (key2.pub).is_zero ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations1;
        ASSERT_LT (iterations1, 200);
    }
    auto balance1 (system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
    auto balance2 (system.clients [0]->ledger.account_balance (key2.pub));
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24002, system.processor));
    ASSERT_FALSE (init1.error ());
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint ());
    auto iterations2 (0);
    while (client1->ledger.account_balance (key2.pub) != balance2)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations2;
        ASSERT_LT (iterations2, 200);
    }
    ASSERT_EQ (balance1, client1->ledger.account_balance (rai::test_genesis_key.pub));
    client1->stop ();
}

TEST (bootstrap_processor, diamond)
{
    rai::system system (24000, 1);
    rai::keypair key;
    std::unique_ptr <rai::send_block> send1 (new rai::send_block);
    send1->hashables.previous = system.clients [0]->ledger.latest (rai::test_genesis_key.pub);
    send1->hashables.destination = key.pub;
    send1->hashables.balance = 100;
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send1->hash (), send1->signature);
    send1->work = system.clients [0]->ledger.create_work (*send1);
    ASSERT_EQ (rai::process_result::progress, system.clients[0]->ledger.process (*send1));
    std::unique_ptr <rai::send_block> send2 (new rai::send_block);
    send2->hashables.previous = send1->hash ();
    send2->hashables.destination = key.pub;
    send2->hashables.balance = 0;
    send2->work = system.clients [0]->ledger.create_work (*send2);
    rai::sign_message (rai::test_genesis_key.prv, rai::test_genesis_key.pub, send2->hash (), send2->signature);
    ASSERT_EQ (rai::process_result::progress, system.clients[0]->ledger.process (*send2));
    std::unique_ptr <rai::open_block> open (new rai::open_block);
    open->hashables.source = send1->hash ();
    open->work = system.clients [0]->ledger.create_work (*open);
    rai::sign_message (key.prv, key.pub, open->hash (), open->signature);
    ASSERT_EQ (rai::process_result::progress, system.clients[0]->ledger.process (*open));
    std::unique_ptr <rai::receive_block> receive (new rai::receive_block);
    receive->hashables.previous = open->hash ();
    receive->hashables.source = send2->hash ();
    rai::sign_message (key.prv, key.pub, receive->hash (), receive->signature);
    receive->work = system.clients [0]->ledger.create_work (*receive);
    ASSERT_EQ (rai::process_result::progress, system.clients[0]->ledger.process (*receive));
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24002, system.processor));
    ASSERT_FALSE (init1.error ());
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint ());
    auto iterations (0);
    while (client1->ledger.account_balance (key.pub) != std::numeric_limits <rai::uint128_t>::max ())
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    ASSERT_EQ (std::numeric_limits <rai::uint128_t>::max (), client1->ledger.account_balance (key.pub));
    client1->stop ();
}

TEST (bootstrap_processor, push_one)
{
    rai::system system (24000, 1);
    rai::client_init init1;
    rai::keypair key1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor));
    auto wallet (client1->wallets.create (rai::uint256_union ()));
    ASSERT_NE (nullptr, wallet);
    wallet->store.insert (rai::test_genesis_key.prv);
    auto balance (client1->ledger.account_balance (rai::test_genesis_key.pub));
    ASSERT_FALSE (wallet->send (key1.pub, 100));
    ASSERT_NE (balance, client1->ledger.account_balance (rai::test_genesis_key.pub));
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint ());
    auto iterations (0);
    while (system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub) == balance)
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    }
    client1->stop ();
}

TEST (frontier_req_response, destruction)
{
    {
        std::shared_ptr <rai::frontier_req_server> hold;
        {
            rai::system system (24000, 1);
            auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.clients [0]));
            std::unique_ptr <rai::frontier_req> req (new rai::frontier_req);
            req->start.clear ();
            req->age = std::numeric_limits <decltype (req->age)>::max ();
            req->count = std::numeric_limits <decltype (req->count)>::max ();
            connection->requests.push (std::unique_ptr <rai::message> {});
            hold = std::make_shared <rai::frontier_req_server> (connection, std::move (req));
        }
    }
    ASSERT_TRUE (true);
}

TEST (frontier_req, begin)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.clients [0]));
    std::unique_ptr <rai::frontier_req> req (new rai::frontier_req);
    req->start.clear ();
    req->age = std::numeric_limits <decltype (req->age)>::max ();
    req->count = std::numeric_limits <decltype (req->count)>::max ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::frontier_req_server> (connection, std::move (req)));
    ASSERT_EQ (connection->client->ledger.store.latest_begin (rai::test_genesis_key.pub), request->iterator);
    auto pair (request->get_next ());
    ASSERT_EQ (rai::test_genesis_key.pub, pair.first);
    rai::genesis genesis;
    ASSERT_EQ (genesis.hash (), pair.second);
}

TEST (frontier_req, end)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.clients [0]));
    std::unique_ptr <rai::frontier_req> req (new rai::frontier_req);
    req->start = rai::test_genesis_key.pub.number () + 1;
    req->age = std::numeric_limits <decltype (req->age)>::max ();
    req->count = std::numeric_limits <decltype (req->count)>::max ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::frontier_req_server> (connection, std::move (req)));
    ASSERT_EQ (connection->client->ledger.store.latest_end (), request->iterator);
    auto pair (request->get_next ());
    ASSERT_TRUE (pair.first.is_zero ());
}

TEST (frontier_req, time_bound)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.clients [0]));
    std::unique_ptr <rai::frontier_req> req (new rai::frontier_req);
    req->start.clear ();
    req->age = 0;
    req->count = std::numeric_limits <decltype (req->count)>::max ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::frontier_req_server> (connection, std::move (req)));
    ASSERT_EQ (connection->client->ledger.store.latest_end (), request->iterator);
    auto pair (request->get_next ());
    ASSERT_TRUE (pair.first.is_zero ());
}

TEST (frontier_req, time_cutoff)
{
    rai::system system (24000, 1);
    auto connection (std::make_shared <rai::bootstrap_server> (nullptr, system.clients [0]));
    std::unique_ptr <rai::frontier_req> req (new rai::frontier_req);
    req->start.clear ();
    req->age = 10;
    req->count = std::numeric_limits <decltype (req->count)>::max ();
    connection->requests.push (std::unique_ptr <rai::message> {});
    auto request (std::make_shared <rai::frontier_req_server> (connection, std::move (req)));
    ASSERT_EQ (connection->client->ledger.store.latest_begin (rai::test_genesis_key.pub), request->iterator);
    auto pair (request->get_next ());
    ASSERT_EQ (rai::test_genesis_key.pub, pair.first);
    rai::genesis genesis;
    ASSERT_EQ (genesis.hash (), pair.second);
}

TEST (bulk, genesis)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor));
    ASSERT_FALSE (init1.error ());
    rai::frontier frontier1;
    ASSERT_FALSE (system.clients [0]->store.latest_get (rai::test_genesis_key.pub, frontier1));
    rai::frontier frontier2;
    ASSERT_FALSE (client1->store.latest_get (rai::test_genesis_key.pub, frontier2));
    ASSERT_EQ (frontier1.hash, frontier2.hash);
    rai::keypair key2;
    ASSERT_FALSE (system.wallet (0)->send (key2.pub, 100));
    rai::frontier frontier3;
    ASSERT_FALSE (system.clients [0]->store.latest_get (rai::test_genesis_key.pub, frontier3));
    ASSERT_NE (frontier1.hash, frontier3.hash);
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint ());
    auto iterations (0);
    while (client1->ledger.latest (rai::test_genesis_key.pub) != system.clients [0]->ledger.latest (rai::test_genesis_key.pub))
    {
        system.service->poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    } 
    ASSERT_EQ (system.clients [0]->ledger.latest (rai::test_genesis_key.pub), client1->ledger.latest (rai::test_genesis_key.pub));
    client1->stop ();
}

TEST (bulk, offline_send)
{
    rai::system system (24000, 1);
    system.wallet (0)->store.insert (rai::test_genesis_key.prv);
    rai::client_init init1;
    auto client1 (std::make_shared <rai::client> (init1, system.service, 24001, system.processor));
    ASSERT_FALSE (init1.error ());
    client1->network.send_keepalive (system.clients [0]->network.endpoint ());
    client1->start ();
    auto iterations (0);
    do
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations;
        ASSERT_LT (iterations, 200);
    } while (system.clients [0]->peers.empty () || client1->peers.empty ());
    rai::keypair key2;
    auto wallet (client1->wallets.create (rai::uint256_union ()));
    wallet->store.insert (key2.prv);
    ASSERT_FALSE (system.wallet (0)->send (key2.pub, 100));
    ASSERT_NE (std::numeric_limits <rai::uint256_t>::max (), system.clients [0]->ledger.account_balance (rai::test_genesis_key.pub));
    client1->processor.bootstrap (system.clients [0]->bootstrap.endpoint ());
    auto iterations2 (0);
    while (client1->ledger.account_balance (key2.pub) != 100)
    {
        system.service->poll_one ();
        system.processor.poll_one ();
        ++iterations2;
        ASSERT_LT (iterations2, 200);
    } ;
	client1->stop ();
}

TEST (network, ipv6)
{
    boost::asio::ip::address_v6 address (boost::asio::ip::address_v6::from_string ("::ffff:127.0.0.1"));
    ASSERT_TRUE (address.is_v4_mapped ());
    boost::asio::ip::udp::endpoint endpoint1 (address, 16384);
    std::vector <uint8_t> bytes1;
    {
        rai::vectorstream stream (bytes1);
        rai::write (stream, address.to_bytes ());
    }
    ASSERT_EQ (16, bytes1.size ());
    for (auto i (bytes1.begin ()), n (bytes1.begin () + 10); i != n; ++i)
    {
        ASSERT_EQ (0, *i);
    }
    ASSERT_EQ (0xff, bytes1 [10]);
    ASSERT_EQ (0xff, bytes1 [11]);
    std::array <uint8_t, 16> bytes2;
    rai::bufferstream stream (bytes1.data (), bytes1.size ());
    rai::read (stream, bytes2);
    boost::asio::ip::udp::endpoint endpoint2 (boost::asio::ip::address_v6 (bytes2), 16384);
    ASSERT_EQ (endpoint1, endpoint2);
}

TEST (network, ipv6_from_ipv4)
{
    boost::asio::ip::udp::endpoint endpoint1 (boost::asio::ip::address_v4::loopback(), 16000);
    ASSERT_TRUE (endpoint1.address ().is_v4 ());
    boost::asio::ip::udp::endpoint endpoint2 (boost::asio::ip::address_v6::v4_mapped (endpoint1.address ().to_v4 ()), 16000);
    ASSERT_TRUE (endpoint2.address ().is_v6 ());
}

TEST (network, ipv6_bind_send_ipv4)
{
    boost::asio::io_service service;
    boost::asio::ip::udp::endpoint endpoint1 (boost::asio::ip::address_v6::any (), 24000);
    boost::asio::ip::udp::endpoint endpoint2 (boost::asio::ip::address_v4::any (), 24001);
    std::array <uint8_t, 16> bytes1;
    auto finish1 (false);
    boost::asio::ip::udp::endpoint endpoint3;
    boost::asio::ip::udp::socket socket1 (service, endpoint1);
    socket1.async_receive_from (boost::asio::buffer (bytes1.data (), bytes1.size ()), endpoint3, [&finish1] (boost::system::error_code const & error, size_t size_a)
    {
        ASSERT_FALSE (error);
        ASSERT_EQ (16, size_a);
        finish1 = true;
    });
    boost::asio::ip::udp::socket socket2 (service, endpoint2);
    boost::asio::ip::udp::endpoint endpoint5 (boost::asio::ip::address_v4::loopback (), 24000);
    boost::asio::ip::udp::endpoint endpoint6 (boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4::loopback ()), 24001);
    socket2.async_send_to (boost::asio::buffer (std::array <uint8_t, 16> {}, 16), endpoint5, [] (boost::system::error_code const & error, size_t size_a)
    {
        ASSERT_FALSE (error);
        ASSERT_EQ (16, size_a);
    });
    while (!finish1)
    {
        service.poll_one ();
    }
    ASSERT_EQ (endpoint6, endpoint3);
    std::array <uint8_t, 16> bytes2;
    auto finish2 (false);
    boost::asio::ip::udp::endpoint endpoint4;
    socket2.async_receive_from (boost::asio::buffer (bytes2.data (), bytes2.size ()), endpoint4, [&finish2] (boost::system::error_code const & error, size_t size_a)
    {
        ASSERT_FALSE (!error);
        ASSERT_EQ (16, size_a);
    });
    socket1.async_send_to (boost::asio::buffer (std::array <uint8_t, 16> {}, 16), endpoint6, [] (boost::system::error_code const & error, size_t size_a)
    {
        ASSERT_FALSE (error);
        ASSERT_EQ (16, size_a);
    });
}