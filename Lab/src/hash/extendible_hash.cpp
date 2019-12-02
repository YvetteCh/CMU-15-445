#include <cassert>
#include <functional>
#include <list>
#include <bitset>
#include <iostream>

#include "hash/extendible_hash.h"
#include "page/page.h"

namespace cmudb {

/*
 * constructor
 * array_size: fixed array size for each bucket
 */

template<typename K, typename V>
ExtendibleHash<K, V>::ExtendibleHash(size_t size):
bucket_size_(size), bucket_count_(0),pair_count_(0),depth(0){
    bucket_.emplace_back(new Bucket(0,0));
}



/*
 * helper function to calculate the hashing address of input key
 * std::hash<>: assumption already has specialization for type K
 * namespace std have standard specializations for basic types.
 */
template<typename K, typename V>
size_t ExtendibleHash<K,V>::HashKey(const K &key) {
    return std::hash<K>()(key);
}



/*
 * helper function to return global depth of hash table
 * NOTE: you must implement this function in order to pass test
 */
template<typename K, typename V>
int ExtendibleHash<K,V>::GetGlobalDepth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return depth;
}


/*
 * helper function to return local depth of one specific bucket
 * NOTE: you must implement this function in order to pass test
 */
template<typename K, typename V>
int  ExtendibleHash<K,V>::GetLocalDepth(int bucket_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if(bucket_[bucket_id]) {
        return bucket_[bucket_id] -> depth;
    }
    return -1;
}



/*
 * helper function to return current number of bucket in hash table
 */
template<typename K, typename V>
int ExtendibleHash<K, V>::GetNumBuckets() const{
    std::lock_guard<std::mutex> lock(mutex_);
    return bucket_count_;
}



/*
 * lookup function to find value associate with input key
 */
template<typename K, typename V>
bool ExtendibleHash<K, V>::Find(const K &key, V &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t position = (std::hash<K>()(key)) & ((1 << depth) - 1);
    if(bucket_.size() >= position && (bucket_[position] -> items).find(key) != (bucket_[position] -> items).end()) {
        value = bucket_[position] -> items[key];
        return true;
    }
    return false;
}


/*
 * delete <key,value> entry in hash table
 * Shrink & Combination is not required for this project
 */
template<typename K, typename V>
bool ExtendibleHash<K, V>::Remove(const K &key) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t position = (HashKey(key)) & ((1 << depth) - 1);
    if(bucket_.size() >= position && (bucket_[position] -> items).find(key) != (bucket_[position] -> items).end()) {
        bucket_[position] -> items.erase(key);
        pair_count_--;
        return true;
    }
    return false;
}



/*
 * insert <key,value> entry in hash table
 * Split & Redistribute bucket when there is overflow and if necessary increase
 * global depth
 */

template<typename K, typename V>
void ExtendibleHash<K, V>::Insert(const K &key, const V &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t position = (HashKey(key)) & ((1 << depth) - 1);

    // bucket is nullptr
    if(bucket_[position] == nullptr) {
        bucket_[position] = std::make_shared<Bucket>(position, depth);
        bucket_count_++;
    }

    // key exits
    if(bucket_[position] -> items.find(key) != bucket_[position] -> items.end()) {
        bucket_[position] -> items[key] = value;
        return;
    }

    // enough space, insert it
    if(bucket_[position]->items.size() < bucket_size_) {
        bucket_[position]->items[key] = value;
        pair_count_++;
        return;
    }

    bucket_[position] -> items[key] = value;
    pair_count_++;

    auto bucket = bucket_[position];
    if(bucket->items.size() > bucket_size_) {
        auto old_index = bucket->id;
        auto old_size = bucket_.size();
        auto old_depth = bucket->depth;
        auto new_bucket = split(bucket);
        
        if(new_bucket->depth > depth) {    
            auto factor = 1<<(new_bucket-> depth - old_depth);
            bucket_.resize(old_size * factor);
            bucket_[bucket->id] = bucket;
            bucket_[new_bucket->id] = new_bucket;

            for (size_t i = 0; i < bucket_.size(); i++) {
                if(bucket_[i]) {
                    auto step = 1 << bucket_[i]->depth;
                    for(size_t j = i + step; j < bucket_.size(); j+= step)
                        bucket_[j] = bucket_[i];
                }
            }
        } else {
            for(size_t i = old_index; i < bucket_.size(); i+=(1<<old_depth)) {
                bucket_[i].reset();
            }
            bucket_[bucket->id] = bucket;
            bucket_[new_bucket->id] = new_bucket;
            auto step = 1 << bucket->depth;
            for(size_t i = bucket->id + step; i < bucket_.size(); i+= step)
                bucket_[i] = bucket_[bucket->id];
            for(size_t i = new_bucket->id + step; i < bucket_.size(); i+= step)
                bucket_[i] = bucket_[new_bucket->id];
        }
    }

}


template<typename K, typename V>
std::shared_ptr<typename ExtendibleHash<K, V>::Bucket> 
ExtendibleHash<K, V>::split(std::shared_ptr<Bucket> &b) {
    auto new_bucket = std::make_shared<Bucket>(0, b->depth);
    while(new_bucket->items.empty()) {
        new_bucket->depth++;
        b->depth++;
        for(auto it = b->items.begin(); it != b->items.end();) {
            if(HashKey(it->first) & (1 << (b->depth - 1))) {
                new_bucket->items.insert({it->first, it->second});
                new_bucket -> id = HashKey(it->first) & ((1 << b->depth) - 1);
                it = b->items.erase(it);
            } else {
                it++;
            }
        }

        if(b->items.empty()) {
            swap(b->items, new_bucket->items);
            b->id = new_bucket->id;
        }
    }

    bucket_count_++;
    return new_bucket;
}




template class ExtendibleHash<page_id_t, Page *>;
template class ExtendibleHash<Page *, std::list<Page *>::iterator>;
// test purpose
template class ExtendibleHash<int, std::string>;
template class ExtendibleHash<int, std::list<int>::iterator>;
template class ExtendibleHash<int, int>;
} // namespace cmudb