#include <iostream>
#include <assert.h>
#include <thread>
#include <tuple>
#include <set>
#include <unordered_map>

#include "Array.hh"
#include "MassTrans.hh"
#include "Logger.hh"
#include "Transaction.hh"
#include "Checkpointer.hh"
#include "Recovery.hh"
#include "ckp_params.hh"
#include "util.hh"

kvepoch_t global_log_epoch = 0;
volatile uint64_t globalepoch = 1;     // global epoch, updated by main thread regularly
kvtimestamp_t initial_timestamp;
volatile bool recovering = false; // so don't add log entries, and free old value immediately

typedef int Key;
typedef int Value;

int main_0() {
  
  Transaction::epoch_advance_callback = [] (unsigned) {
    // just advance blindly because of the way Masstree uses epochs
    globalepoch++;
  };
  
  std::string log("./disk1");
  const std::vector<std::string> logfiles({log});
  const std::vector<std::vector<unsigned> > assignments_given;
  root_folder = "./silo_log";
  pthread_t advancer;
  pthread_create(&advancer, NULL, Transaction::epoch_advancer, NULL);
  pthread_detach(advancer);
  MassTrans<Value> h;
  h.set_tree_id(1);
  
  std::vector<MassTrans<Value> *>tree_list;
  tree_list.push_back(&h);
  ckpdirs = {
    {"./f0/disk1/", "./f0/disk2/",
      "./f0/disk3/", "./f0/disk4/",
      "./f0/disk5/", "./f0/disk6/"},
    
    {"./f1/disk1/", "./f1/disk2/",
      "./f1/disk3/", "./f1/disk4/",
      "./f1/disk5/", "./f1/disk6/"},
    
    {"./f2/disk1/", "./f2/disk2/",
      "./f2/disk3/", "./f2/disk4/",
      "./f2/disk5/", "./f2/disk6/"},
    
    {"./data/disk1/", "./data/disk2/",
      "./data/disk3/", "./data/disk4/",
      "./data/disk5/", "./data/disk6/"},
  };
  
  //Logger::Init(1,logfiles, assignments_given); // call_fsync = true, use_compression = false, fake_writes = false
  //Checkpointer::Init(&tree_list, logfiles, true); // is_test = true
  
  h.thread_init();
  Transaction::threadid = 0;
  int threadid = Transaction::threadid;
  Transaction t;

  
  h.transWrite(t, 1, 1);
  h.transWrite(t, 3, 3);
  
  assert(t.commit());
  uint64_t tid_1 = Transaction::tinfo[0].last_commit_tid;
  
  Transaction::threadid = 2;
  Transaction tt;
  h.transRead(tt, 3);
  
  Transaction::threadid = 0;
  Transaction tm;
  h.transDelete(tm, 3);
  assert(tm.commit());
  
  uint64_t tid_2 = Transaction::tinfo[0].last_commit_tid;
  
  assert(tid_1 < tid_2);
  
  Transaction::threadid = 1;
  
  /*Transaction t1;
  h.transWrite(t1, 4, 4);
  assert(t1.commit());
  uint64_t tid_3 = Transaction::tinfo[1].last_commit_tid;*/
  
  Transaction t2;
  h.transWrite(t2, 3, 1);
  assert(t2.commit());
  uint64_t tid_4 = Transaction::tinfo[1].last_commit_tid;
  
  assert(tid_4 > tid_2);
  Transaction::threadid = 2;
  assert(!tt.commit());
  
  
  //Transaction

}

struct txn_record_multi {
  // keeps track of updates and removes for a single transaction
  // updates and removes also contain information about tree ID
  
  // the "key" here consistitutes of table ID, followed by key inserted/removed
  std::map<std::pair<uint64_t, uint64_t>, int> updates;
  std::set<std::pair<uint64_t, uint64_t> > removes;
};

void consistent_test_multi_table_thread(uint64_t low, uint64_t high,
                                        std::map<uint64_t, txn_record_multi *> *txn_list,
                                        int th,
                                        int new_insert, int num_new_insert,
                                        std::vector<MassTrans<Value> *> *trees) {
  // this thread should perform a random set of key inserts and removes
  // for each transaction that it performs, it keeps tracks of the returned TID
  // and the state of the txn
  
  std::cout << "Starting thread " << th << "\n";
  Transaction::threadid = th;
  
  int add = 3;
  int remove = 1;
  
  int cur_num_new_insert = 0;

  int num_txn = 100;
  int count_txn = 0;
  
  int num_tree_ids = trees->size();
  
  int interval = high - low;
  int x = 10000000;
  
  while(count_txn++ < num_txn) {
    txn_record_multi *tr = new txn_record_multi;
    
    Transaction t;
    int value = x;
    
    // make sure the insert keys and remove keys are all unique
    std::set<std::pair<uint64_t, int> > repeat_key_list;
    
    std::pair<int, int> insert_key, remove_key;
    MassTrans<Value> *btr;
    bool if_insert = false;
    
    for (int i = 0; i < add; i++) {
      // insert/update
      do {
        int key = rand() % interval + low;
        int idx = rand() % num_tree_ids;
        btr = (*trees)[idx];
        uint64_t table = (*trees)[idx]->get_tree_id();
        insert_key = std::make_pair(table, key);
      } while(repeat_key_list.find(insert_key) != repeat_key_list.end());
      
      std::cout << "[" << th << "] inserted key " <<insert_key.second << " to tree " << insert_key.first << std::endl;

      repeat_key_list.insert(insert_key);
      btr->transWrite(t, insert_key.second, value);
      tr->updates[insert_key] = x;
    }
    
    for (int j = 0; j < remove; j++) {
      do {
        int key = rand() % interval + low;
        int idx = rand() % num_tree_ids;
        btr = (*trees)[idx];
        uint64_t table = (*trees)[idx]->get_tree_id();
        remove_key = std::make_pair(table, key);
      } while (repeat_key_list.find(remove_key) != repeat_key_list.end());
      repeat_key_list.insert(remove_key);
      bool deleted = btr->transDelete(t, remove_key.second);
      if (deleted) {
        tr->removes.insert(remove_key);
        std::cout << "[" << th << "] removed key " <<remove_key.second << " from tree " << remove_key.first << std::endl;
      }
      //<< " success " << deleted << std::endl;
     }
  
    if (rand() % 10 == 0 && cur_num_new_insert < num_new_insert) {
      // try to insert key in all of the tables
      int int_key =  new_insert + cur_num_new_insert;
      // value is just the same as key
      for (int i = 0; i < num_tree_ids; i++) {
        btr = (*trees)[i];
        bool inserted = btr->transInsert(t, int_key, int_key);
        if (inserted)
          tr->updates[std::make_pair(btr->get_tree_id(), int_key)] = int_key;
      }
      
      if_insert = true;
    }
    
    if (t.commit()) {
      // put this transaction in its record
      uint64_t final_commit_tid = Transaction::tinfo[th].last_commit_tid;
      assert((*txn_list).find(final_commit_tid) == (*txn_list).end());
      (*txn_list)[final_commit_tid] = tr;
      x++;
      if (if_insert) {
        cur_num_new_insert++;
      }
    }
    if_insert = false;
  }
  
  std::cout << "Ending thead " << th << "\n";
}


// consistent_test_multi_table
int main() {
  
  Transaction::epoch_advance_callback = [] (unsigned) {
    // just advance blindly because of the way Masstree uses epochs
    globalepoch++;
  };
  
  for (int i = 0; i < 8; i++) {
    Transaction::tinfo[i].last_commit_tid = 0;
  }
  
  Transaction::threadid = 0;
  
  Transaction::global_epoch = 0;
  pthread_t advancer;
  pthread_create(&advancer, NULL, Transaction::epoch_advancer, NULL);
  pthread_detach(advancer);

  root_folder = "./silo_log";

  enable_par_ckp = 1;
  reduced_ckp = 1;
  
  ckpdirs = {
    {"./f0/disk1/", "./f0/disk2/",
      "./f0/disk3/", "./f0/disk4/",
      "./f0/disk5/", "./f0/disk6/"},
    
    {"./f1/disk1/", "./f1/disk2/",
      "./f1/disk3/", "./f1/disk4/",
      "./f1/disk5/", "./f1/disk6/"},
    
    {"./f2/disk1/", "./f2/disk2/",
      "./f2/disk3/", "./f2/disk4/",
      "./f2/disk5/", "./f2/disk6/"},
    
    {"./data/disk1/", "./data/disk2/",
      "./data/disk3/", "./data/disk4/",
      "./data/disk5/", "./data/disk6/"},
  };
  
  const std::vector<std::string> logfiles({
    std::string("./data/"),
    std::string("./f0/"),
    std::string("./f1/"),
    std::string("./f2/"),
  });
  
  const std::vector<std::vector<unsigned> > assignments_given;
  
  uint64_t low = 0;
  uint64_t high = 1000;
  
  std::vector<MassTrans<Value> *> btree_list;
  
  MassTrans<Value> btr1;
  btr1.set_tree_id(1);
  
  MassTrans<Value> btr2;
  btr2.set_tree_id(2);
  
  MassTrans<Value> btr3;
  btr3.set_tree_id(3);
  
  MassTrans<Value> btr4;
  btr4.set_tree_id(4);
  
  btree_list.push_back(&btr1);
  btree_list.push_back(&btr2);
  btree_list.push_back(&btr3);
  btree_list.push_back(&btr4);
  
  // Starting logging and checkpointing
  Logger::Init(logfiles.size(),logfiles, assignments_given); // call_fsync = true, use_compression = false, fake_writes = false
  Checkpointer::Init(&btree_list, logfiles, true); // is_test = true
 
  // we first insert all of the keys into the database
  for (size_t j = 0; j < btree_list.size(); j++) {
    MassTrans<Value> *btr = btree_list[j];
    btr->thread_init();
    for (uint64_t i = low; i <= high; i++) {
      Transaction t;
      btr->transWrite(t, i, 0);
      t.commit();
    }
  }
  
  std::vector<std::thread> threads;
  int num_th = 4;
  
  std::vector<std::map<uint64_t, txn_record_multi *> > txn_list;
  for (int i = 0; i < num_th; i++) {
    txn_list.emplace_back();
  }

  
  for(int i = 0; i < num_th; i++) {
    threads.emplace_back(&consistent_test_multi_table_thread, low, high,
                         &(txn_list[i]), i,
                         high + i * 4000, 3000, &btree_list);
  }

  sleep(60);
  //printf("Calling wait_for_idle_state()\n");
  //Logger::wait_for_idle_state();
  //printf("Idle state reached\n");
  
  for (int i = 0; i < num_th; i++) {
    threads[i].join();
  }
  
  uint64_t new_epoch;
  uint64_t cepoch;
  int fd = open(std::string(root_folder).append("/cepoch").c_str(), O_RDONLY);
  if (fd > 0) {
    if (read(fd, (char *) &cepoch, 8) < 0) {
      perror("Checkpoint epoch reading failed");
      assert(false);
    }
    close(fd);
  } else {
    perror("cepoch file not found");
    //assert(false);
  }
  
  uint64_t pepoch;
  fd = open(std::string(root_folder).append("/pepoch").c_str(), O_RDONLY);
  if (fd > 0) {
    if (read(fd, (char *) &pepoch, 8) < 0) {
      perror("Persist error reading failed");
      assert(false);
    }
    close(fd);
  } else {
    perror("pepoch file not found");
    assert(false);
  }
  
  new_epoch = pepoch - 13;
  fprintf(stderr, "cepoch: %lu, new_epoch: %lu, pepoch: %lu\n", cepoch, new_epoch, pepoch);
  
  // We should reconstruct a big map with TIDs
  // this map will automatically reorder all of the TIDs through iterator
  
  std::map<uint64_t, txn_record_multi *> tlist;
  int count = 0;
  for (size_t i = 0; i < txn_list.size(); i++) {
    count += txn_list[i].size();
    tlist.insert(txn_list[i].begin(), txn_list[i].end()); // TODO: deal with repeated tids
  }
  assert(tlist.size() == count);
  
  
  std::cout << "Replay single threaded" << std::endl;
  
  std::map<std::pair<uint64_t, uint64_t>, int> updates; //(<tableId, key>, value)
  std::set<std::pair<uint64_t, uint64_t> > removes;
  
  for (size_t j = 0; j < btree_list.size(); j++) {
    uint64_t tree_id = btree_list[j]->get_tree_id();
    for (uint64_t i = low; i <= high; i++) {
      updates[std::make_pair(tree_id, i)] = 0;
    }
  }
  
  std::map<uint64_t, txn_record_multi *>::iterator it = tlist.begin();
  std::map<uint64_t, txn_record_multi *>::iterator it_end = tlist.end();

  
  for (; it != it_end; it++) {
    uint64_t cur_epoch = epochId(it->first);
    
    if (cur_epoch > new_epoch)
      continue;
    
    txn_record_multi *r = it->second;
    
    std::map<std::pair<uint64_t, uint64_t>, int>::iterator u_it = r->updates.begin();
    std::map<std::pair<uint64_t, uint64_t>, int>::iterator u_it_end = r->updates.end();
    
    std::set<std::pair<uint64_t, uint64_t> >::iterator find_it;
    
    for (; u_it != u_it_end; u_it++) {
     // std::cout << " Updating tree " << u_it->first.first << " key "<<u_it->first.second << " value " <<u_it->second << std::endl;
      updates[u_it->first] = u_it->second;
      find_it = removes.find(u_it->first);
      if (find_it != removes.end()) {
        removes.erase(find_it);
      }
    }
    
    std::set<std::pair<uint64_t, uint64_t> >::iterator r_it = r->removes.begin();
    std::set<std::pair<uint64_t, uint64_t> >::iterator r_it_end = r->removes.end();
    
    std::map<std::pair<uint64_t, uint64_t>, int>::iterator find_u_it;
    
    for (; r_it != r_it_end; r_it++) {
      removes.insert(*r_it);
      find_u_it = updates.find(*r_it);
      if (find_u_it != updates.end()) {
        updates.erase(find_u_it);
      }
    }
  }
  
  // Actually replay the logs using recovery
  
  std::cout << "Starting recovery..." << std::endl;
  
  Recovery::Init();
  Recovery::txn_btree_map_type *tree_map = Recovery::recover(logfiles, new_epoch);
  
  std::cout << "Recovery complete" << std::endl;
  
  assert(tree_map->size() == 4);
  std::cout << "Starting consistency check " << std::endl;
  
  bool consistent = true;
  
  std::map<std::pair<uint64_t, uint64_t>, int>::iterator r_u_it = updates.begin();
  std::map<std::pair<uint64_t, uint64_t>, int>::iterator r_u_it_end = updates.end();
  
  Transaction t;
  for (; r_u_it != r_u_it_end; r_u_it++) {
    uint64_t id = (r_u_it->first).first;
    uint64_t int_key = (r_u_it->first).second;
    int int_value = r_u_it->second;
    std::string key = std::to_string(int_key);
    int v0;
    
    bool found = (*tree_map)[id]->transGet(t, key, v0);
    if (!found) {
      consistent = false;
      (*tree_map)[id]->transGet(t, key, v0);
      std::cout << "Key" << int_key << " is not found in tree " << id << "\n";
    }
    
    if (v0 != int_value) {
      consistent = false;
      (*tree_map)[id]->transGet(t, key, v0);
      std::cout << " In tree " << id << ", Key " << int_key << " should have value " << int_value << ", but has value " << v0 <<"\n";
    }
  }
  
  std::set<std::pair<uint64_t, uint64_t> >::iterator r_r_it = removes.begin();
  std::set<std::pair<uint64_t, uint64_t> >::iterator r_r_it_end = removes.end();
  
  for (; r_r_it != r_r_it_end; r_r_it++) {
    uint64_t id = r_r_it->first;
    uint64_t int_key = r_r_it->second;
    std::string key = std::to_string(int_key);
    int v0;
    bool found = (*tree_map)[id]->transGet(t, key, v0);
    if (found) {
      consistent = false;
      std::cout<< " In tree " << id  << ", Key " << int_key << " was not removed!\n";
    }
  }

  
  if (consistent) {
    printf("PASS\n");
  } else {
    printf("FAIL\n");
  }
  
  it = tlist.begin();
  it_end = tlist.end();
  for (; it != it_end; it++) {
    delete it->second;
  }
}