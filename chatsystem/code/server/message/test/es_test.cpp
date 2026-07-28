#include <iostream>
#include <string>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <thread>

#include "../../common/es_client.hpp"

int main(int argc, char* argv[]) {
    std::srand(std::time(nullptr));
    
    std::string es_host = "127.0.0.1";
    int es_port = 9200;
    std::string index_name = "message";
    std::string doc_type = "_doc";
    
    // Create ES client
    std::vector<std::string> hosts;
    hosts.push_back("http://" + es_host + ":" + std::to_string(es_port) + "/");
    
    std::cout << "[TEST] Creating ES client connecting to: " << hosts[0] << std::endl;
    es::ESClient client(hosts, 3000);
    
    // Step 1: Delete existing index
    std::cout << "[TEST] Deleting index..." << std::endl;
    client.delete_index(index_name);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Step 2: Create index with mapping
    std::cout << "[TEST] Creating index with mapping..." << std::endl;
    std::vector<es::ESClient::FieldProperty> fields = {
        {"chat_session_id", "keyword", "", true},
        {"message_id", "keyword", "", true},
        {"content", "text", "ik_max_word", true},
    };
    if (!client.create_index(index_name, doc_type, fields)) {
        std::cerr << "[TEST] Failed to create index!" << std::endl;
        return 1;
    }
    std::cout << "[TEST] Index created successfully" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Step 3: Insert test documents
    std::string session_id = "test_session_" + std::to_string(std::rand() % 10000);
    std::cout << "[TEST] Using session_id: " << session_id << std::endl;
    
    for (int i = 0; i < 5; i++) {
        Json::Value doc;
        doc["chat_session_id"] = session_id;
        doc["message_id"] = "msg_" + std::to_string(i);
        doc["content"] = "This is test message " + std::to_string(i) + " with keyword search_token_xyz";
        
        bool ok = client.insert_data(index_name, doc_type, "msg_" + std::to_string(i), doc);
        std::cout << "[TEST] Insert msg_" << i << ": " << (ok ? "OK" : "FAILED") << std::endl;
    }
    
    // Wait for indexing
    std::cout << "[TEST] Waiting 2 seconds for indexing..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Step 4: Test via ESClient wrapper
    std::cout << "\n[TEST] === ESClient search via wrapper ===" << std::endl;
    {
        // Build query exactly as MsgSearch does
        Json::Value query;
        query["query"]["bool"]["must"][0]["term"]["chat_session_id"] = session_id;
        query["query"]["bool"]["must"][1]["match"]["content"] = "search_token_xyz";
        
        // Print the query for debugging
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::cout << "[TEST] Query JSON: " << Json::writeString(builder, query) << std::endl;
        
        Json::Value result;
        bool ok = client.search_data(index_name, doc_type, query, result);
        std::cout << "[TEST] search_data returned: " << (ok ? "true" : "false") << std::endl;
        
        if (ok) {
            int hits = 0;
            if (result["hits"]["total"].isObject()) {
                hits = result["hits"]["total"]["value"].asInt();
            } else {
                hits = result["hits"]["total"].asInt();
            }
            std::cout << "[TEST] Hits via wrapper: " << hits << std::endl;
            
            // Print all hits
            for (const auto& hit : result["hits"]["hits"]) {
                std::cout << "  - id: " << hit["_id"].asString() 
                          << ", content: " << hit["_source"]["content"].asString() << std::endl;
            }
        }
    }
    
    // Step 5: Test match_all
    std::cout << "\n[TEST] === match_all test ===" << std::endl;
    {
        Json::Value query;
        query["query"]["match_all"] = Json::Value(Json::objectValue);
        
        Json::Value result;
        bool ok = client.search_data(index_name, doc_type, query, result);
        std::cout << "[TEST] match_all search: " << (ok ? "true" : "false") << std::endl;
        
        if (ok) {
            int hits = 0;
            if (result["hits"]["total"].isObject()) {
                hits = result["hits"]["total"]["value"].asInt();
            } else {
                hits = result["hits"]["total"].asInt();
            }
            std::cout << "[TEST] match_all hits: " << hits << std::endl;
        }
    }
    
    // Step 6: Test simple term query
    std::cout << "\n[TEST] === Simple term query ===" << std::endl;
    {
        Json::Value query;
        query["query"]["term"]["chat_session_id"] = session_id;
        
        Json::Value result;
        bool ok = client.search_data(index_name, doc_type, query, result);
        std::cout << "[TEST] term query: " << (ok ? "true" : "false") << std::endl;
        
        if (ok) {
            int hits = 0;
            if (result["hits"]["total"].isObject()) {
                hits = result["hits"]["total"]["value"].asInt();
            } else {
                hits = result["hits"]["total"].asInt();
            }
            std::cout << "[TEST] term query hits: " << hits << std::endl;
        }
    }
    
    // Step 7: Test simple match query
    std::cout << "\n[TEST] === Simple match query ===" << std::endl;
    {
        Json::Value query;
        query["query"]["match"]["content"] = "search_token_xyz";
        
        Json::Value result;
        bool ok = client.search_data(index_name, doc_type, query, result);
        std::cout << "[TEST] match query: " << (ok ? "true" : "false") << std::endl;
        
        if (ok) {
            int hits = 0;
            if (result["hits"]["total"].isObject()) {
                hits = result["hits"]["total"]["value"].asInt();
            } else {
                hits = result["hits"]["total"].asInt();
            }
            std::cout << "[TEST] match query hits: " << hits << std::endl;
        }
    }
    
    // Step 8: Bool query with separate term and match (not in must)
    std::cout << "\n[TEST] === Bool should query (alternative) ===" << std::endl;
    {
        Json::Value query;
        query["query"]["bool"]["filter"][0]["term"]["chat_session_id"] = session_id;
        query["query"]["bool"]["must"][0]["match"]["content"] = "search_token_xyz";
        
        Json::Value result;
        bool ok = client.search_data(index_name, doc_type, query, result);
        std::cout << "[TEST] bool query (filter+must): " << (ok ? "true" : "false") << std::endl;
        
        if (ok) {
            int hits = 0;
            if (result["hits"]["total"].isObject()) {
                hits = result["hits"]["total"]["value"].asInt();
            } else {
                hits = result["hits"]["total"].asInt();
            }
            std::cout << "[TEST] bool query hits: " << hits << std::endl;
        }
    }
    
    std::cout << "\n[TEST] All tests completed!" << std::endl;
    return 0;
}