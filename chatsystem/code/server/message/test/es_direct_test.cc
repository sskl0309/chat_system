#include <iostream>
#include <string>
#include <cstdlib>
#include <ctime>
#include "../../common/es_client.hpp"
#include "../../common/log.hpp"

int main() {
    // Initialize logging
    mylog::init(true, "", mylog::LogLevel::DEBUG);
    
    std::srand(std::time(nullptr));
    
    std::string es_host = "127.0.0.1";
    int es_port = 9200;
    std::string index_name = "message";
    std::string doc_type = "_doc";
    
    // Create ES client
    auto client = std::make_shared<es::ESClient>(
        std::vector<std::string>{"http://" + es_host + ":" + std::to_string(es_port) + "/"},
        3000
    );
    
    std::cout << "[TEST] ES client created" << std::endl;
    
    // Clean up: delete index if exists
    std::cout << "[TEST] Deleting existing index..." << std::endl;
    client->delete_index(index_name);
    usleep(500000);  // Wait for deletion
    
    // Create index with mapping
    std::cout << "[TEST] Creating index..." << std::endl;
    std::vector<es::ESClient::FieldProperty> fields = {
        {"chat_session_id", "keyword", "", true},
        {"message_id", "keyword", "", true},
        {"content", "text", "ik_max_word", true},
    };
    if (!client->create_index(index_name, doc_type, fields)) {
        std::cerr << "[TEST] Failed to create index" << std::endl;
        return 1;
    }
    usleep(500000);  // Wait for index creation
    
    // Insert test documents
    std::string session_id = "test_session_" + std::to_string(std::rand());
    std::cout << "[TEST] Using session_id: " << session_id << std::endl;
    
    for (int i = 0; i < 5; i++) {
        Json::Value doc;
        doc["chat_session_id"] = session_id;
        doc["message_id"] = "msg_" + std::to_string(i);
        doc["content"] = "This is test message number " + std::to_string(i) + 
                         " with keyword test_search_xyz";
        
        if (!client->insert_data(index_name, doc_type, "msg_" + std::to_string(i), doc)) {
            std::cerr << "[TEST] Failed to insert document " << i << std::endl;
            return 1;
        }
        std::cout << "[TEST] Inserted document " << i << std::endl;
    }
    
    // Wait for indexing
    std::cout << "[TEST] Waiting for indexing..." << std::endl;
    usleep(2000000);  // Wait 2 seconds
    
    // Test 1: Match all query
    std::cout << "\n[TEST 1] Match all query:" << std::endl;
    {
        Json::Value query;
        query["query"]["match_all"] = Json::Value(Json::objectValue);
        
        Json::Value result;
        if (client->search_data(index_name, doc_type, query, result)) {
            int hits = result["hits"]["total"]["value"].asInt();
            std::cout << "  Hits: " << hits << std::endl;
            for (const auto& hit : result["hits"]["hits"]) {
                std::cout << "  - " << hit["_source"]["message_id"].asString() 
                          << ": " << hit["_source"]["content"].asString() << std::endl;
            }
        } else {
            std::cerr << "  Search failed" << std::endl;
        }
    }
    
    // Test 2: Term query on chat_session_id
    std::cout << "\n[TEST 2] Term query on chat_session_id:" << std::endl;
    {
        Json::Value query;
        query["query"]["term"]["chat_session_id"] = session_id;
        
        Json::Value result;
        if (client->search_data(index_name, doc_type, query, result)) {
            int hits = result["hits"]["total"]["value"].asInt();
            std::cout << "  Hits: " << hits << std::endl;
        } else {
            std::cerr << "  Search failed" << std::endl;
        }
    }
    
    // Test 3: Match query on content
    std::cout << "\n[TEST 3] Match query on content:" << std::endl;
    {
        Json::Value query;
        query["query"]["match"]["content"] = "test_search_xyz";
        
        Json::Value result;
        if (client->search_data(index_name, doc_type, query, result)) {
            int hits = result["hits"]["total"]["value"].asInt();
            std::cout << "  Hits: " << hits << std::endl;
        } else {
            std::cerr << "  Search failed" << std::endl;
        }
    }
    
    // Test 4: Bool must query (the exact query used by MsgSearch)
    std::cout << "\n[TEST 4] Bool must query (MsgSearch query):" << std::endl;
    {
        Json::Value query;
        query["query"]["bool"]["must"][0]["term"]["chat_session_id"] = session_id;
        query["query"]["bool"]["must"][1]["match"]["content"] = "test_search_xyz";
        
        std::cout << "  Query JSON: " << Json::writeString(Json::StreamWriterBuilder().indentation(""), query) << std::endl;
        
        Json::Value result;
        if (client->search_data(index_name, doc_type, query, result)) {
            int hits = result["hits"]["total"]["value"].asInt();
            std::cout << "  Hits: " << hits << std::endl;
            for (const auto& hit : result["hits"]["hits"]) {
                std::cout << "  - " << hit["_source"]["message_id"].asString() 
                          << ": " << hit["_source"]["content"].asString() << std::endl;
            }
        } else {
            std::cerr << "  Search failed" << std::endl;
        }
    }
    
    // Test 5: Direct elasticlient search
    std::cout << "\n[TEST 5] Direct elasticlient search:" << std::endl;
    {
        Json::Value query;
        query["query"]["bool"]["must"][0]["term"]["chat_session_id"] = session_id;
        query["query"]["bool"]["must"][1]["match"]["content"] = "test_search_xyz";
        
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::string body = Json::writeString(builder, query);
        
        std::cout << "  URL: /" << index_name << "/" << doc_type << "/_search" << std::endl;
        std::cout << "  Body: " << body << std::endl;
        
        auto response = client->get_client()->search(index_name, doc_type, body);
        std::cout << "  Status: " << response.status_code << std::endl;
        std::cout << "  Response: " << response.text.substr(0, 500) << std::endl;
    }
    
    std::cout << "\n[TEST] All tests completed" << std::endl;
    return 0;
}