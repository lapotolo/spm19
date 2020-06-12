#ifndef FF_FARM_TSP_H
#define FF_FARM_TSP_H


#include <ff/utils.hpp>
#include <ff/ff.hpp>
#include <ff/parallel_for.hpp>
#include <ff/pipeline.hpp>
#include <ff/farm.hpp>

#define DEBUG 1
#define DEBUG_W 0

// #include "conf.hpp"

/*
This module implements a Master-Workers ff_Farm to solve genetic TSP.
In particular the Master as an Emitter has the duty of splitting jobs
between avaiable workers while as a Collector has the duty to merge the results
returned by the workers.
*/


struct Gen_TSP_FF_Data_ptrs
{
  std::shared_ptr<std::vector<std::vector<int>>> pop;
  std::shared_ptr<std::vector<int>> fit_values;
  std::shared_ptr<std::function<int32_t(std::vector<int> const&)>> fit_fun;
  std::shared_ptr<std::pair<int32_t, std::vector<int>>> curr_opt;
};


// it may represent both a range when emitted by the master and and a pair of index
// (when collected by the same master)
// where fst_idx is the position in population
// of the optimum of a single chunk while snd_idx is the position of the current worst
struct TSP_Task
{
  size_t fst_idx; // start | best
  size_t snd_idx; // end   | worst
  Gen_TSP_FF_Data_ptrs ptrs; // we need to pass around pointers to data to be elaborated by farm's nodes
};


struct TSP_Master : ff::ff_monode_t<TSP_Task >
{
  // FIELDS
  size_t num_workers;
  size_t max_epochs;
  size_t population_size;
  
  size_t curr_epoch;
  size_t dispatched_curr_gen; // counter for tasks already sent in the current generation
  size_t received_curr_gen;   // counter for tasks completed in the current generation

  std::vector<TSP_Task> workers_results_to_merge;

  Gen_TSP_FF_Data_ptrs master_ptrs; // helper structs containing pointers to data structures of the problem

  // CTOR
  TSP_Master( size_t nw
            , size_t max_its
            , size_t pop_s
            , std::shared_ptr<std::vector<std::vector<int>>> pop
            , std::shared_ptr<std::vector<int>> fit_values
            , std::shared_ptr<std::function<int32_t(std::vector<int> const&)>> fit_fun
            , std::shared_ptr<std::pair<int32_t, std::vector<int>>> curr_opt
            )
            : num_workers(nw)
            , max_epochs(max_its)
            , population_size(pop_s)
            , master_ptrs({pop, fit_values, fit_fun, curr_opt})
            , curr_epoch(0)
            , dispatched_curr_gen(0)
            , received_curr_gen(0)
  {}

  // split jobs and send them to workers
  void dispatch_tasks();

  // merge the results sent back by workers
  void selection(std::vector<TSP_Task> & workers_results);

  // business logic code
  TSP_Task* svc(TSP_Task* tsp_task);

};

struct TSP_Worker : ff::ff_node_t< TSP_Task, TSP_Task > 
{

  TSP_Task* svc(TSP_Task* tsp_task);

  void crossover(TSP_Task & task);
  void mutate(TSP_Task & task);
  TSP_Task* evaluate_population(TSP_Task & task);

};


// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// FARM MASTER METHODS IMPLEMENTATION
void TSP_Master::dispatch_tasks()
{
  size_t i, step;
  step = population_size / (num_workers); // SET THE STEP PROPERLY. (PAR. SLACK)
  for (i = 0; i + step - 1 < population_size; i += step) // FIX REMAINING PIECES USING size_t remained = size % threshold;
  {
    auto to_send = new TSP_Task{i, i+step-1 ,master_ptrs};
    std::cout<< "            Sending: ["<< to_send->fst_idx << ", " << to_send->snd_idx << "]\n";
    ff_send_out(to_send);
    dispatched_curr_gen++;
  }
}

void TSP_Master::selection(std::vector<TSP_Task> & workers_results)
{
  auto pointer_pack = workers_results[0].ptrs;

  auto curr_gen_min_idx = workers_results[0].fst_idx;
  auto curr_gen_max_idx = workers_results[0].snd_idx;

  auto curr_gen_min_val = pointer_pack.curr_opt->first;
  auto curr_gen_max_val = curr_gen_min_val;
      
  for(auto & t : workers_results)
  {
    // check if we have a new minimum for the current generation 
    if((*pointer_pack.fit_values)[t.fst_idx] < curr_gen_min_val)
    {
      curr_gen_min_idx = t.fst_idx;
      curr_gen_min_val = (*pointer_pack.fit_values)[t.fst_idx];
    }
    // check if we have a new maximum for the current generation 
    else if((*pointer_pack.fit_values)[t.snd_idx] > curr_gen_max_val)
    {
      curr_gen_max_idx = t.snd_idx;
      curr_gen_max_val = (*pointer_pack.fit_values)[t.snd_idx];
    }
  }

  // if in this generation we found a new optimum
  // then we record it in the proper a class field
  if(curr_gen_min_val < pointer_pack.curr_opt->first)
  {
    pointer_pack.curr_opt->first  = curr_gen_min_val;
    pointer_pack.curr_opt->second = (*pointer_pack.pop)[curr_gen_min_idx];
  }
  // inject the global optimum from previous generations in the current generation
  // in place of the worst chromosome of the current generation    
  (*pointer_pack.fit_values)[curr_gen_max_idx] = pointer_pack.curr_opt->first;
  (*pointer_pack.pop)[curr_gen_max_idx]        = pointer_pack.curr_opt->second;
}

// TSP_Master
TSP_Task* TSP_Master::svc(TSP_Task* tsp_task)
{
  std::cout<< "\n  >> [START] Am I gonna send or receive?\n             curr_epoch: " << curr_epoch <<"\n";
  if(tsp_task == nullptr) // && dispatched_curr_gen == 0)
  {
    std::cout<< "\n  >> [SEND] As the Master I'm gonna dispatch, curr_epoch: "<< curr_epoch <<"\n";
    dispatch_tasks();
    std::cout<< "  >> [SEND] Finished dispatching, tasks dispatched curr gen: "<< dispatched_curr_gen << "\n";
    return GO_ON;
  }
  // store each workers' result in a vector on which we will perform selection
  else if(tsp_task != nullptr)
  {
    workers_results_to_merge.push_back(*tsp_task);
    delete tsp_task;
#if DEBUG    
    std::cout<< "\n  >> [RECEIVE] Master received something different from nullptr\n";
    std::cout<< "               POST-push_back results size: " << workers_results_to_merge.size() << "\n";  
#endif
  }
  if(workers_results_to_merge.size() == dispatched_curr_gen) // if every worker sent back its result for the current gen
  {
#if DEBUG
    std::cout<<"\n  >> [IN IF] So we have (Size of results) == (Task dispatched), that is : " << workers_results_to_merge.size() << " == " << dispatched_curr_gen<< "\n";
    std::cout<<"  >> [IN IF] chromo_fitness:   [ ";
    for(auto e : (*master_ptrs.fit_values)) std::cout<<e << " ";
    std::cout<<" ]\n";
    std::cout<<"  >> [IN IF] Results this gen: [ ";
    for(auto e : workers_results_to_merge) std::cout<< "(min_idx="<<e.fst_idx << ", MAX_idx=" <<e.snd_idx<<") ";
    std::cout<< "]\n  >>         Now Master will perform selection\n";
#endif
    selection(workers_results_to_merge); // merge subresult received from workers
#if DEBUG
    std::cout<<"  >> [IN IF] Finished Selection"<<"\n";
    std::cout<<"  >> [IN IF] CURR GLOB MIN VAL : " << master_ptrs.curr_opt->first <<"\n";
    std::cout<<"  >> [IN IF] CURR GLOB MIN TOUR: [ ";
    for(auto e : (master_ptrs.curr_opt->second)) std::cout<<e << " ";
    std::cout<<"]\n";
    std::cout<<"  >> [IN IF] Increasing epoch. From: " << curr_epoch << " to " << curr_epoch+1 <<"\n";
#endif
    dispatched_curr_gen = 0;
    workers_results_to_merge.clear();
    if( ++curr_epoch == max_epochs) {std::cout<<"  >> Gonna EOS. I'm sick and tired to work with these workers!\n"; return EOS;}
#if DEBUG
    std::cout<< "  >> [IN IF] I could not send EOS."<<"\n";
#endif
    dispatch_tasks();
  }
  std::cout<< "\n  >> [AFTER IF] I will send GO_ON. I need to work some more..."<<"\n";
  return GO_ON; // go next epoch. Right?
}

// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

// FARM WORKERS METHODS IMPLEMENTATION

// OK
void TSP_Worker::crossover(TSP_Task & task)
{
  auto pointer_pack = task.ptrs;

  size_t i, j, left, right;
  size_t chromosome_size = (pointer_pack.pop->at(0)).size();

  std::random_device rd;  // get a seed for the random number engine
  std::mt19937 gen(rd()); // standard mersenne_twister_engine seeded with rd()

  std::discrete_distribution<> biased_coin({ 1-CROSSOVER_PROB, CROSSOVER_PROB });
  
  for(i=task.fst_idx; i < task.snd_idx-1; i+=2)
  {
    if(biased_coin(gen))
    {
      std::uniform_int_distribution<> left_distr(1, ((chromosome_size)/2)-1);
      std::uniform_int_distribution<> right_distr(chromosome_size/2, chromosome_size-2);
      left  = left_distr(gen);
      right = right_distr(gen);

      // setup the structures to build in the end two feasible offspings
      std::deque<int> tmp_chromo, missing;
      std::vector<int> counter_1(chromosome_size, 0), counter_2(chromosome_size, 0);

      for(j = left; j <= right; ++j) tmp_chromo.push_back((*pointer_pack.pop)[i][j]);
      // copy central part of second parent into the central part of the first parent
      for(j = left; j <= right; ++j) (*pointer_pack.pop)[i][j] = (*pointer_pack.pop)[i+1][j];
      // viceversa, copy central part of first parent into the central part of the second parent
      for(j = left; j <= right; ++j) { (*pointer_pack.pop)[i+1][j] = tmp_chromo.front(); tmp_chromo.pop_front(); }

      // SANITIZE PHASE
      // count number of occurrences for each symbol in both the two new offsprings
      for(j = 0; j < chromosome_size; ++j) { counter_1[(*pointer_pack.pop)[i][j]]++; counter_2[(*pointer_pack.pop)[i+1][j]]++; }

      // use a deque to keep track of missing numbers of the first offspring on the front
      // and missing numbers of the second offspring in the back
      for(j = 0; j < chromosome_size; ++j) { if(counter_1[j] == 0 ) missing.push_front(j); if(counter_2[j] == 0 ) missing.push_back(j); }
      if(missing.size())
      {
        // replace doubles entries with the ones in missing
        for(j = 0; j < chromosome_size; ++j)
        {
          if(counter_1[(*pointer_pack.pop)[i][j]] == 2)
          {
            counter_1[(*pointer_pack.pop)[i][j]]--;
            counter_1[missing.front()]++;
            (*pointer_pack.pop)[i][j] = missing.front();
            missing.pop_front();
          }
          if(counter_2[(*pointer_pack.pop)[i+1][j]] == 2)
          {
            counter_2[(*pointer_pack.pop)[i+1][j]]--;
            counter_2[missing.back()]++;
            (*pointer_pack.pop)[i+1][j] = missing.back();
            missing.pop_back();
          }
        }
      } // end if(missing.size())
    } // end if(biased_cpid)
  } //end for(chunk...)
}

// OK
void TSP_Worker::mutate(TSP_Task & task)
{
  auto pointer_pack = task.ptrs;
  size_t i;
  size_t chromosome_size = (pointer_pack.pop->at(0)).size();

  std::random_device rd;  // get a seed for the random number engine
  std::mt19937 gen(rd()); // standard mersenne_twister_engine seeded with rd()

  std::discrete_distribution<> biased_coin({ 1-MUTATION_PROB, MUTATION_PROB });
  std::uniform_int_distribution<> idx_distr(0, chromosome_size-1);

  for(i=task.fst_idx; i < task.snd_idx; ++i)
    if( biased_coin(gen))
      std::swap((*pointer_pack.pop)[i][idx_distr(gen)], (*pointer_pack.pop)[i][idx_distr(gen)]); // thread safe?
}

// TO CHECK!
TSP_Task* TSP_Worker::evaluate_population(TSP_Task & task)
{
  size_t i;
  auto pointer_pack = task.ptrs;
  auto sub_pop_min_idx = task.fst_idx;
  auto sub_pop_max_idx = task.fst_idx;

  auto sub_pop_min_val = (*pointer_pack.fit_fun)((*pointer_pack.pop)[sub_pop_min_idx]);
  auto sub_pop_max_val = sub_pop_min_val;

  for(i=task.fst_idx; i <= task.snd_idx; ++i) // here the right end of the range is included in the computed range!
  { 
    (*pointer_pack.fit_values)[i] = (*pointer_pack.fit_fun)((*pointer_pack.pop)[i]);
    // looking for new best individual
    if((*pointer_pack.fit_values)[i] < sub_pop_min_val)
    {
      sub_pop_min_val = (*pointer_pack.fit_values)[i];
      sub_pop_min_idx = i;
    }
    // looking for new worst individual
    if((*pointer_pack.fit_values)[i] > sub_pop_max_val)
    {
      sub_pop_max_val = (*pointer_pack.fit_values)[i];
      sub_pop_max_idx = i;
    }
  }
  return new TSP_Task{sub_pop_min_idx, sub_pop_max_idx, task.ptrs};
}

// CHECK!
TSP_Task* TSP_Worker::svc(TSP_Task* tsp_task)
{
  TSP_Task &t = *tsp_task;
  crossover(*tsp_task);
  mutate(*tsp_task);
  auto to_send = evaluate_population(*tsp_task);
  //std::cout<<"!! EVALUATION FINISHED. THE WORKER IS GONNA SEND STUFF BACK\n";
  return to_send;
  //ff_send_out(to_send);
  //return GO_ON;
}

#endif // FF_FARM_TSP_H
