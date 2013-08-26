// -*-c++-*-
//-------------------------------------------------------------------------
// Copyright (c) 2012 The Regents of the University of Texas System.
// All rights reserved.
//
// Redistribution and use in source and binary forms are permitted
// provided that the above copyright notice and this paragraph are
// duplicated in all such forms and that any documentation,
// advertising materials, and other materials related to such
// distribution and use acknowledge that the software was developed by
// the University of Texas.  The name of the University may not be
// used to endorse or promote products derived from this software
// without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
// WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
//
//-------------------------------------------------------------------------
// A SLURM derived showq for summarizing user jobs.
//
// Originally: 7/29/2012 (refactor of original LSF version)
//
// Karl W. Schulz- Texas Advanced Computing Center (karl@tacc.utexas.edu)
//
// $Id: slurm_showq.C 1473 2012-12-02 20:34:38Z karl $
//-------------------------------------------------------------------------*/ 

#include <iostream>
#include <set>
#include <string>
#include "GetPot"

using namespace std;

#include <sys/types.h>
#include <pwd.h>
#include <libgen.h>
#include <cstdio>
#include <limits.h>
#include <unistd.h>
#include <time.h>
#include <slurm/slurm.h>

#include <cstring>
#include <cstdlib>
#include <vector>

#define VERSION 0.11

class Slurm_Showq {

 public:
  Slurm_Showq();
  void query_running_jobs();
  void parse_supported_options(int argc, char *argv[]);


private:

  std::string uid_to_string(uid_t id);
  std::string get_executable_path();

  void print_usage();
  void print_version();
  char *getusername();	       
  void read_runtime_config(std::string file);	
  
  // Command line controls

  bool  show_blocked;
  bool  long_listing;
  bool  user_jobs_only;
  bool  named_user_jobs_only;
  std::string named_user;
  bool	named_partition_jobs_only;
  std::string  named_partition;
  bool  show_all_reserv;
  bool  show_utilization;
  bool  sort_by_priority;
  float RES_LEAD_WINDOW;

  std::string progname;		 // binary name for stdout
  int total_avail_nodes;	 // max # of nodes (for utilization calculations)
  int total_avail_procs;
};
  



