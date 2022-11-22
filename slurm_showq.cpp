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
// $Id: slurm_showq.cpp 1776 2013-04-17 21:17:04Z karl $
//-------------------------------------------------------------------------*/ 
//
//    Copyright (C) 2013 Karl Schulz, Paul Edmon
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//--------------------------------------------------------------------------*/


#include "slurm_showq.h"

void Slurm_Showq::query_running_jobs()
{
  char *user, *starttime, *shorttime, *pendreason, *endtime, *submittime;
  time_t current_time, remain_time;
  hostlist_t job_nodelist;
  char *host;
  char *token;
  char *delimiters = ",";
  char *deleq = "=";
  char jobuser_short  [256];
  char jobname_short  [256];
  char queuename_short[256];
  char tres[256];
  char gres[256];
  int i, j, k, n, numhl, status;
  int pend_id[100000], new_id_order[100000], priority[100000], id_temp[100000];
  bool sorted;
  bool partition_true;

  int rjobs  = 0;
  int rprocs = 0;
  int rgpus  = 0;
  int rnodes = 0;
  int alreadycounted;
  int total_avail_gpus;

  int dprocs, dgpus, dnodes;
  int jobgpu;
  int ijobs, bjobs, error_jobs;
  int running_jobs; 
  int reserv_procs;
  int numHosts = 1;
  long hours_max, secs_max;
  long hours_remain, mins_remain, secs_remain;

  int num_pending_jobs    = 0;
  int num_completing_jobs = 0;
  int idle_jobs    = 0;
  int blocked_jobs = 0;
  int num_closed   = 0;
  char queuename[8];
  char current_user[256];
  char current_partition[256];
  char hostlist[10000][256]; /* Stores current host list */
  char hostlistcheck[10000][256]; /* Stores what hosts we have already checked */
  int num_nodes;
  long int time1, time2;

  //  const int total_avail_procs = 5760*16;

  std::vector<uint32_t> completing_jobs; // list of jobs in completing state
  std::vector<uint32_t> pending_jobs;	 // list of jobs in pending state
  std::vector<uint32_t> errored_jobs;	 // list of jobs with a problem

  typedef struct koomie_job_struct
  {
    int jobId;
    char *submittime;
    long hours_remain;
    long mins_remain;
    long secs_remain;
    char *jobname;
    char *user;
    char *queue;
    int numProcessors;
    int ptile;
    int iflag :4;		/* iflag = 0: new job */
                                /* iflag = 1: idle    */ 
                                /* iflag = 2: held    */
                                /* iflag = 3: blocked */ 
  } koomie_job_struct;

  koomie_job_struct *Pend_jobs;

  if (named_user_jobs_only)
    strncpy(current_user,named_user.c_str(),255);
  else
    strncpy(current_user,getusername(),255);
	
  bool local_user_jobs_only=(user_jobs_only || named_user_jobs_only);
  
  if(local_user_jobs_only)
    printf("\nSUMMARY OF JOBS FOR USER: <%s>\n",current_user);

  /* Checking if we need jobs from only a specific queue and grabbing the name for that queue.  Note that queue and partition are used interchangably */
  if (named_partition_jobs_only) strncpy(current_partition,named_partition.c_str(),255);

  /* Printing out header if we are only looking at a specific queue */
  if (named_partition_jobs_only) printf("\nSUMMARY OF JOBS FOR QUEUE: <%s>\n\n",current_partition);

  //----------------
  // Query all jobs 
  //----------------

  job_info_msg_t *job_ptr = NULL;

  if( slurm_load_jobs( (time_t) NULL, &job_ptr, SHOW_ALL) )
    {
      printf("[Error]: unable to query SLURM jobs\n");
      exit(10);
    } 

  //slurm_print_job_info_msg(stdout,job_ptr,0);
  
  running_jobs = 1;

  std::string userString;

  /*--------------
   * showq Header 
   *--------------*/

  if(running_jobs)
    {
     if(!summary_only)
      {
       printf("ACTIVE JOBS--------------------\n");
      if(long_listing)
	{
	  printf("JOBID     JOBNAME    USERNAME      STATE   CORE   GPU    NODE QUEUE         REMAINING  STARTTIME\n");
	  printf("=======================================================================================================\n");
	}
      else
	{
	  printf("JOBID     JOBNAME    USERNAME      STATE   CORE   GPU    REMAINING  STARTTIME\n");
	  printf("====================================================================================\n");
	}
      }
    

      /*-------------------------------
       * 1st: Display all running jobs 
       *-------------------------------*/
      
      for(int i=0; i<job_ptr->record_count;i++)
	{

	  job_info_t * job;
	  job = &job_ptr->job_array[i];

	  // get username string

	  userString = uid_to_string(job->user_id);

	  // get partition string

      strncpy(queuename_short,job->partition,255);

	  // keep track of pending jobs...

	  if(job->job_state == JOB_PENDING)
	    {
	      num_pending_jobs++;
	      pending_jobs.push_back(i);
	      continue;
	    }

	  current_time = time(NULL);
	  secs_max     = job->time_limit*60; // max runtime (converted to secs)
	  remain_time  = (time_t)difftime(secs_max+job->start_time,current_time);

	  // keep track of completing jobs - those which have exceeded
	  // their runlimit by a given threshold are flagged as
	  // errored.

	  if(job->job_state & JOB_COMPLETING)
	    {
	      completing_jobs.push_back(i);

	      // allow 5 minute grace window

	      if(remain_time < 300) 
		continue;

	      // ignore non-user jobs with "-u" flag

	      if ((local_user_jobs_only) && 
		  (strcmp(current_user,uid_to_string(job->user_id).c_str()) != 0) )
		continue;

	      if ((named_partition_jobs_only) && 
		  (strcmp(current_partition,queuename_short) != 0) )
		continue;

	      errored_jobs.push_back(i);
	      continue;
	    }

	  // only displaying running jobs currently...
	  if( job->job_state != JOB_RUNNING )
	    continue;
	  if ((local_user_jobs_only) && 
	      (strcmp(current_user,uid_to_string(job->user_id).c_str()) != 0) )
	    continue;
	  if ((named_partition_jobs_only) && 
		  (strcmp(current_partition,queuename_short) != 0) )
		continue;

	  // Gather time info
	  
	  starttime = strdup(ctime( &job->start_time));
	  starttime[strlen(starttime)-1] = '\0';
					  
	  hours_remain = remain_time/3600;
	  mins_remain  = (remain_time-hours_remain*3600)/60;
	  secs_remain  = (remain_time - hours_remain*3600 - mins_remain*60);
	  
	  if(hours_remain < 0 || mins_remain < 0 || secs_remain < 0)
	    {	 
	      printf("[Warn]: runlimit exhausted: runningpending job reason = %i\n",job->state_reason);
	      continue;
	    }

      // Calculate GPU usage
      jobgpu = 0;
      strncpy(tres,job->tres_req_str,255);

      if (strstr(tres,"gpu"))
      {
	    token = strtok(tres,delimiters);

        for(;;) {
          if (strstr(token,"gpu"))
          {
            token = strtok(token,deleq);
            token = strtok(NULL,deleq);
            jobgpu = atoi(token);
            break;
          }
		  if (token == NULL) break;
		  token = strtok(NULL,delimiters);
        }
      }

	  // Display job info
        if(!summary_only)
          {    
	  printf("%-10i", job->job_id);  
	  }

	  if(NULL != job->name)
		strncpy(jobname_short,job->name,10);
	  else
		strncpy(jobname_short,"(null)",10);
	  jobname_short[10] = '\0';
	  if(!summary_only)
	    {
	    printf("%-11s",jobname_short); 
	    }
	  strncpy(jobuser_short,uid_to_string(job->user_id).c_str(),14);
	  jobuser_short[14] = '\0';
	  if(!summary_only)
	    {
	    printf("%-14s", jobuser_short);

	    printf("%-8s","Running");
	    printf("%-6i ",job->num_cpus);;
	    printf("%-6i ",jobgpu);;
	    }
	  if(long_listing)
	    {
	      if(!summary_only)
		{
	        printf("%-4i",job->num_nodes);
		}
	      //printf("%-4i",6000);
	      strncpy(queuename_short,job->partition,14);
	      queuename_short[14] = '\0';
	      if(!summary_only)
		{
	        printf(" %-14s",queuename_short); 
		}
	    }
	 if(!summary_only)
	  {         
	  printf(" %2i:",hours_remain);
	  if(mins_remain < 10)
	    printf("0%1i:",mins_remain);
	  else
	    printf("%2i:",mins_remain);
	  
	  if(secs_remain < 10)
	    printf("0%1i  ",secs_remain);
	  else
	    printf("%2i  ",secs_remain);
	  
	  shorttime = (char *)calloc(20,sizeof(char));
	  strncpy(shorttime,starttime,19);
	  shorttime[19] = '\0';
	  printf("%s",shorttime);
	  
	  printf("\n");
          }
	  free(starttime);
	  free(shorttime);
	  
	  rjobs++;
	  rprocs += job->num_cpus;
      rgpus += jobgpu;

	  /* Need to make sure we aren't double counting nodes */
      /* Lets get the current hostlist */
	  job_nodelist = slurm_hostlist_create(job->nodes);

	  k=0;

	  while ((host = slurm_hostlist_shift(job_nodelist))) {
		strncpy(hostlist[k],host,255);
		k++;
	  }

	  numhl=k;

      /* Now lets check it against the nodes we have already counted */
	  for (k=0;k<numhl;k++){

		alreadycounted=0;

		for(n=0;n<rnodes;n++){
		  if (strcmp(hostlist[k],hostlistcheck[n]) == 0) {
			alreadycounted=1;
			break;
		    }
		  }

		/* If we've already been counted break out of this loop if not counted then store that data */
		if (alreadycounted == 0) {
		  strcpy(hostlistcheck[rnodes],hostlist[k]);
		  rnodes++;
		  }
        }
	  

	} // end loop over slurm jobs

      /* Running Job summary*/

	/* We need to figure out how many cores and nodes are available for this partition if we specify one */
	if(named_partition_jobs_only) {
	  partition_info_msg_t * partition_ptr = NULL;

      if( slurm_load_partitions( (time_t) NULL, &partition_ptr, SHOW_ALL) ) {
      	printf("[Error]: unable to query SLURM partitions\n");
        exit(10);
      }

      for(int i=0; i<partition_ptr->record_count;i++) {

	  	partition_info_t * partition;
	  	partition = &partition_ptr->partition_array[i];

	  	// get partition string

      	strncpy(queuename_short,partition->name,255);

	    if ((strcmp(current_partition,queuename_short) != 0) ) continue;

		total_avail_procs = partition->total_cpus;
		total_avail_nodes = partition->total_nodes;

        // Calculate GPU usage
        total_avail_gpus = 0;
        strncpy(tres,partition->tres_fmt_str,255);

        if (strstr(tres,"gpu"))
        {
	      token = strtok(tres,delimiters);

          for(;;) {
            if (strstr(token,"gpu"))
            {
              token = strtok(token,deleq);
              token = strtok(NULL,deleq);
              total_avail_gpus = atoi(token);
              break;
            }
		    if (token == NULL) break;
		    token = strtok(NULL,delimiters);
          }
        }
	  }
	}
      
	if(local_user_jobs_only)
	  {
	    //printf("\nYou have %3i Active jobs utilizing: %6i of %6i Processors (%4.2f%%)\n",rjobs,rprocs,
	    //total_avail_procs,(float)100.*rprocs/total_avail_procs);
	  }
	else
	  {
	    printf("\n");
	    if(rjobs == 1)
	      printf( "%6i active job  ",rjobs);
	    else
	      printf( "%6i active jobs ",rjobs);

	    if(show_utilization)
	      printf(": %4i of %4i nodes (%6.2f %%)",rnodes,total_avail_nodes,(float)100.*rnodes/total_avail_nodes);

	    if(named_partition_jobs_only) {
	      printf(": %4i of %4i cores (%6.2f %%)",rprocs,total_avail_procs,(float)100.*rprocs/total_avail_procs);
          printf(": %4i of %4i gpus (%6.2f %%)",rgpus,total_avail_gpus,(float)100.*rgpus/max(1,total_avail_gpus));
	      printf(": %4i of %4i nodes (%6.2f %%)",rnodes,total_avail_nodes,(float)100.*rnodes/total_avail_nodes);
		}

	    printf("\n");

	  }

    }

  //---------------------------------------------------------------------
  // 2nd: Display pending jobs; 1st come idle jobs waiting for free
  // resources, then come blocked jobs
  // ---------------------------------------------------------------------

  if(pending_jobs.size() > 0)
    { 
     if(!summary_only)
       {
      printf("\nWAITING JOBS------------------------\n");
      if(long_listing)
	{
	  printf("JOBID     JOBNAME    USERNAME      STATE   CORE   GPU    HOST  QUEUE           WCLIMIT  QUEUETIME\n");
	  printf("======================================================================================================\n");
	}
      else
	{
	  printf("JOBID     JOBNAME    USERNAME      STATE   CORE   GPU     WCLIMIT  QUEUETIME\n");
	  printf("====================================================================================\n");
	}
      }
    /* We are going to sort the jobs in order of priorty */
    if (sort_by_priority) {

		/* First we need to extract the priority data */
		for (int i=0; i<pending_jobs.size();i++) {
	  		job_info_t * job;
	  		job = &job_ptr->job_array[pending_jobs[i]];
			pend_id[i]=i;
			priority[i]=job->priority;
		}

		/* Now we can actually sort the data */
		new_id_order[0]=pend_id[0];
		for (int i=1; i<pending_jobs.size();i++) {
			sorted=false;
			for (int j=0;j<i;j++) {
				if (priority[pend_id[i]] > priority[new_id_order[j]]) {
					for (int k=j; k<i; k++) {
						id_temp[k]=new_id_order[k];
					}
					new_id_order[j]=pend_id[i];
					for (int k=j; k<i; k++) {
						new_id_order[k+1]=id_temp[k];
					}
					sorted=true;
					break;
				}
			}
			if (sorted == false) {
				new_id_order[i]=pend_id[i];
			}
		}

    }


      for(int i=0; i<pending_jobs.size();i++)
	{

	  job_info_t * job;
	  job = &job_ptr->job_array[pending_jobs[i]];

	  if (sort_by_priority) {
		job = &job_ptr->job_array[pending_jobs[new_id_order[i]]];
	  }

	  // get partition string

      strncpy(queuename_short,job->partition,255);

/*	  printf("Partition Name: %s\n",queuename_short);

	  token = strtok(queuename_short,delimiters);

	  printf("Split Partition Name: %s\n",token); */

	  if(job->job_state != JOB_PENDING)
	    continue;
	  if ((local_user_jobs_only) && 
	      (strcmp(current_user,uid_to_string(job->user_id).c_str()) != 0) )
	    continue;
	  /*if ((named_partition_jobs_only) && 
		  (strcmp(current_partition,queuename_short) != 0) )
		continue; */

      if (named_partition_jobs_only) {
		partition_true = 1;
		token = strtok(queuename_short,delimiters);
		for(;;) {
		  /*printf("Split Partition %s\n",token);*/
		  if (token == NULL) break;
		  if (strcmp(current_partition,token) == 0) partition_true = 0;
		  token = strtok(NULL,delimiters);
        }
      }

	  if ((named_partition_jobs_only) && (partition_true)) continue;

	  if(job->state_reason == WAIT_DEPENDENCY               ||
	     job->state_reason == WAIT_HELD                     ||
	     job->state_reason == WAIT_TIME                     ||
	     job->state_reason == WAIT_ASSOC_JOB_LIMIT          ||
	     job->state_reason == WAIT_QOS_MAX_CPU_PER_JOB      ||
	     job->state_reason == WAIT_QOS_MAX_CPU_MINS_PER_JOB ||
	     job->state_reason == WAIT_QOS_MAX_NODE_PER_JOB     ||
	     job->state_reason == WAIT_QOS_MAX_WALL_PER_JOB     ||
	     job->state_reason == WAIT_HELD_USER                ||
         job->state_reason == FAIL_BAD_CONSTRAINTS          ||
         job->state_reason == WAIT_PART_CONFIG              ||
         job->state_reason == WAIT_ARRAY_TASK_LIMIT)
	    {
	      blocked_jobs++;
	      continue;
	    }

	  if(job->state_reason != WAIT_PRIORITY                 && 
	     job->state_reason != WAIT_RESOURCES                &&
	     job->state_reason != WAIT_NODE_NOT_AVAIL           &&
	     job->state_reason != WAIT_PART_NODE_LIMIT          &&
	     job->state_reason != WAIT_PART_TIME_LIMIT          && 
	     job->state_reason != FAIL_DOWN_PARTITION           && 
	     job->state_reason != WAIT_PART_DOWN                && 
	     job->state_reason != WAIT_NO_REASON                && 
	     job->state_reason != WAIT_QOS_JOB_LIMIT            &&
	     job->state_reason != WAIT_QOS_RESOURCE_LIMIT       &&
	     job->state_reason != WAIT_QOS_TIME_LIMIT           &&
	     job->state_reason != WAIT_QOS_MAX_CPU_PER_USER     &&
	     job->state_reason != WAIT_QOS_MAX_JOB_PER_USER     &&
	     job->state_reason != WAIT_QOS_MAX_NODE_PER_USER    &&
	     job->state_reason != WAIT_QOS_MAX_SUB_JOB          &&
	     job->state_reason != WAIT_RESERVATION )
	    {
	      printf("[Warn]: unknown job state - pending job reason = %i\n",job->state_reason);
	      continue;
	    }

	  idle_jobs++;

	  // Display job info
         if(!summary_only)
	   {
	  printf("%-10i", job->job_id);
	   }
	  if(NULL != job->name)
		strncpy(jobname_short,job->name,10);
	  else
		strncpy(jobname_short,"(null)",10);
	  jobname_short[10] = '\0';
          if(!summary_only)
	    {
	  printf("%-11s",jobname_short);

	  strncpy(jobuser_short,uid_to_string(job->user_id).c_str(),14);
	  jobuser_short[14] = '\0';
	  printf("%-14s", jobuser_short);

	  printf("%-8s","Waiting");
	  printf("%-6i ",job->num_cpus);

      // Calculate GPU usage
      jobgpu = 0;
      strncpy(tres,job->tres_req_str,255);

      if (strstr(tres,"gpu"))
      {
	    token = strtok(tres,delimiters);

        for(;;) {
          if (strstr(token,"gpu"))
          {
            token = strtok(token,deleq);
            token = strtok(NULL,deleq);
            jobgpu = atoi(token);
            break;
          }
		  if (token == NULL) break;
		  token = strtok(NULL,delimiters);
        }
      }

      printf("%-6i ",jobgpu);

	  if(long_listing)
	    {
	      printf("%-4i ",job->num_nodes);
	      strncpy(queuename_short,job->partition,14);
	      queuename_short[14] = '\0';
	      printf("%-14s",queuename_short); 
	    }

	  secs_max     = job->time_limit*60; // max runtime (converted to secs)
	  hours_remain = secs_max/3600;
	  mins_remain  = (secs_max-hours_remain*3600)/60;
	  secs_remain  = (secs_max - hours_remain*3600 - mins_remain*60);

	  printf(" %2i:",hours_remain);
	  if(mins_remain < 10)
	    printf("0%1i:",mins_remain);
	  else
	    printf("%2i:",mins_remain);
	  
	  if(secs_remain < 10)
	    printf("0%1i  ",secs_remain);
	  else
	    printf("%2i  ",secs_remain);

	  submittime = strdup(ctime( &job->submit_time));

	  shorttime = (char *)calloc(20,sizeof(char));
	  strncpy(shorttime,submittime,19);

	  printf("%s",shorttime);
	  printf("\n");
	    }
	  free(submittime);
	  free(shorttime);

	} //  end loop over slurm jobs

    }	  // end for pending jobs

  if(blocked_jobs > 0)
    {
      if(!summary_only)
	{
      printf("\nBLOCKED JOBS--\n");
      if(long_listing)
	{
	  printf("JOBID     JOBNAME    USERNAME      STATE   CORE   GPU    HOST  QUEUE           WCLIMIT  QUEUETIME\n");
	  printf("=======================================================================================================\n");
	}
      else
	{
	  printf("JOBID     JOBNAME    USERNAME      STATE   CORE   GPU     WCLIMIT  QUEUETIME\n");
	  printf("====================================================================================\n");
	}
       }
      for(int i=0; i<pending_jobs.size();i++)
	{

	  job_info_t * job;
	  job = &job_ptr->job_array[pending_jobs[i]];

	  	  if(job->job_state != JOB_PENDING)
	  	    continue;

	  if(job->state_reason != WAIT_DEPENDENCY               &&
	     job->state_reason != WAIT_HELD                     &&
	     job->state_reason != WAIT_TIME                     &&
	     job->state_reason != WAIT_ASSOC_JOB_LIMIT          &&
	     job->state_reason != WAIT_QOS_MAX_CPU_PER_JOB      &&
	     job->state_reason != WAIT_QOS_MAX_CPU_MINS_PER_JOB &&
	     job->state_reason != WAIT_QOS_MAX_NODE_PER_JOB     &&
	     job->state_reason != WAIT_QOS_MAX_WALL_PER_JOB     &&
	     job->state_reason != WAIT_HELD_USER                &&
         job->state_reason != FAIL_BAD_CONSTRAINTS          &&
         job->state_reason != WAIT_PART_CONFIG              &&
         job->state_reason != WAIT_ARRAY_TASK_LIMIT)
	    {
	      continue;
	    }

	    // Only display blocked jobs to the user who owns them, or root.
	    if( (strcmp(current_user,uid_to_string(job->user_id).c_str()) != 0) && (strcmp(current_user,"root") != 0) && (true!=show_blocked) )
		continue;

	  // Display job info
	 if(!summary_only)
	   {
	  printf("%-10i", job->job_id);
	   }
	  if(NULL != job->name)
		strncpy(jobname_short,job->name,10);
	  else
		strncpy(jobname_short,"(null)",10);
	  jobname_short[10] = '\0';
          if(!summary_only)
	    {
	  printf("%-11s",jobname_short);

	  strncpy(jobuser_short,uid_to_string(job->user_id).c_str(),14);
	  jobuser_short[14] = '\0';
	  printf("%-14s", jobuser_short);

	  if(job->state_reason == WAIT_ASSOC_JOB_LIMIT)
	  	printf("%-8s","Quota");
	  else
	  	printf("%-8s","Waiting");

	  printf("%-6i ",job->num_cpus);

      // Calculate GPU usage
      jobgpu = 0;
      strncpy(tres,job->tres_req_str,255);

      if (strstr(tres,"gpu"))
      {
	    token = strtok(tres,delimiters);

        for(;;) {
          if (strstr(token,"gpu"))
          {
            token = strtok(token,deleq);
            token = strtok(NULL,deleq);
            jobgpu = atoi(token);
            break;
          }
		  if (token == NULL) break;
		  token = strtok(NULL,delimiters);
        }
      }

      printf("%-6i ",jobgpu);

	  if(long_listing)
	    {
	      printf("%-4i ",job->num_nodes);
	      strncpy(queuename_short,job->partition,14);
	      queuename_short[14] = '\0';
	      printf("%-14s",queuename_short); 
	    }

	  secs_max     = job->time_limit*60; // max runtime (converted to secs)
	  hours_remain = secs_max/3600;
	  mins_remain  = (secs_max-hours_remain*3600)/60;
	  secs_remain  = (secs_max - hours_remain*3600 - mins_remain*60);

	  printf(" %2i:",hours_remain);
	  if(mins_remain < 10)
	    printf("0%1i:",mins_remain);
	  else
	    printf("%2i:",mins_remain);
	  
	  if(secs_remain < 10)
	    printf("0%1i  ",secs_remain);
	  else
	    printf("%2i  ",secs_remain);

	  submittime = strdup(ctime( &job->submit_time));

	  shorttime = (char *)calloc(20,sizeof(char));
	  strncpy(shorttime,submittime,19);

	  printf("%s",shorttime);
	  printf("\n");
	 }
	  free(submittime);
	  free(shorttime);
	} // end loop over jobs



  //-----------------------------------------------------
  // 4th: Summarize completing/errored jobs
  //----------------------------------------------------

  if(errored_jobs.size() > 0)
    {
     if(!summary_only)
       {
      printf("\nCOMPLETING/ERRORED JOBS-------------\n");
      printf("JOBID     JOBNAME    USERNAME      STATE   CORE     WCLIMIT  QUEUETIME\n");
      printf("================================================================================\n");
       }
      for(int i=0; i<errored_jobs.size();i++)
	{
	  job_info_t * job;
	  job = &job_ptr->job_array[errored_jobs[i]];

	  current_time = time(NULL);
	  secs_max     = job->time_limit*60; // max runtime (converted to secs)
	  remain_time  = (time_t)difftime(secs_max+job->start_time,current_time);
	  starttime    = strdup(ctime( &job->start_time));

	  starttime[strlen(starttime)-1] = '\0';
					  
	  hours_remain = remain_time/3600;
	  mins_remain  = (remain_time-hours_remain*3600)/60;
	  secs_remain  = (remain_time - hours_remain*3600 - mins_remain*60);

	  /* only show leading minus sign */
	  
	  if(mins_remain < 0)
	    mins_remain = -mins_remain;
	  if(secs_remain < 0)
	    secs_remain = -secs_remain;

	  if(NULL != job->name)
		strncpy(jobname_short,job->name,10);
	  else
		strncpy(jobname_short,"(null)",10);
	  jobname_short[10] = '\0';
	  strncpy(jobuser_short,uid_to_string(job->user_id).c_str(),14);
	  jobuser_short[14] = '\0';
	  if(!summary_only)
	    {
	  printf("%-10i", job->job_id);
	  printf("%-11s",jobname_short);
	  printf("%-14s", jobuser_short);
	  printf("%-8s","Complte");
	  printf("%-6i ",job->num_cpus);

	  printf(" %2i:",hours_remain);
	  if(mins_remain < 10)
	    printf("0%1i:",mins_remain);
	  else
	    printf("%2i:",mins_remain);
	  
	  if(secs_remain < 10)
	    printf("0%1i  ",secs_remain);
	  else
	    printf("%2i  ",secs_remain);

	  shorttime = (char *)calloc(20,sizeof(char));
	  strncpy(shorttime,starttime,19);
	  shorttime[19] = '\0';
	  printf("%s",shorttime);
	  printf("\n");
	    }
	  free(starttime);
	  free(shorttime);

	} // end loop over errored jobs
    
    }
 }
  /*----------
   * Summary
   *----------*/
  
  printf("\nTotal Jobs: %-4i  Active Jobs: %-4i  Idle Jobs: %-4i  "
	 "Blocked Jobs: %-4i\n",rjobs+idle_jobs+blocked_jobs,
	 rjobs,idle_jobs,blocked_jobs);
//
//  /*------------------------------
//   * Query advanced reservations 
//   *------------------------------*/
//
//    resInfo = lsb_reservationinfo(NULL, &resvNumbers, 0);
//
//    if(resvNumbers > 0)
//      {
//
//	printf("\nADVANCED RESERVATIONS----------\n");
//	printf("RESV ID     PROC                    RESERVATION WINDOW\n");
//	
//	for(i=0;i<resvNumbers;i++)
//	  {
//	    sscanf(resInfo[i].timeWindow,"%ld-%ld",&time1,&time2);
//
//	    /* Weed out reservations that are a long way out */
//
//	    if( (difftime(time1,time(NULL))) > RES_LEAD_WINDOW && !show_all_reserv)
//	      continue;
//
//	    starttime = strdup(ctime(&time1));
//	    endtime   = strdup(ctime(&time2));
//	    starttime[strlen(starttime)-1] = '\0';
//	    endtime[strlen(endtime)-1]     = '\0';
//
//	    /* Count up the number of reserved processors */ 
//
//	    reserv_procs = 0;
//	    for(j=0;j<resInfo[i].numRsvHosts;j++)
//	      reserv_procs += resInfo[i].rsvHosts[j].numRsvProcs;
//
//	    printf("%-10s  %4i    %s - %s\n",resInfo[i].rsvId,
//		   reserv_procs,starttime,endtime);
//
//
//
//	    free(starttime);
//	    free(endtime);
//	      
//	  }
//      }

  // clean-up time

  slurm_free_job_info_msg(job_ptr);

  exit(0);

}

/*-------------------------------------------------------------------
 * quantify_pending_job(): 
 * 
 * Determine job idleness by comparing lsb_pendreason to certain 
 * key pending reasons.  Idle jobs are defined as:
 *
 *    (a) those that do not have enough processes available
 *    (b) freshly submitted jobs
 *
 * Deferred jobs are the inverse.
 *---------------------------------------------------------------------*/

int quantify_pending_job(char *reason)
{

  const char *idle_string1 = "New job is waiting for scheduling";
  const char *idle_string2 = "Not enough processors";
  const char *idle_string3 = "Job was suspended";
  const char *idle_string4 = "Job\'s requirement for exclusive execution not satisfied";
  char *local;

  if(strstr(reason,idle_string1) != NULL)
    return(0);
  else if(strstr(reason,idle_string2) != NULL)
    return(1);
  else if(strstr(reason,idle_string3) != NULL)
    return(2);
  else if(strstr(reason,idle_string4) != NULL)
    return(1);
  else
    return(3);
}

/*-------------------------------------
 * Parse/Define command line arguments
 *-------------------------------------*/

#if 0
void parse_command_line(int argc,char **argv)
{
  /* Allowable command line options */

#if 1
  struct arg_lit *l        = arg_lit0("l", NULL, "long (wide) listing");
  struct arg_lit *u        = arg_lit0("u", NULL, "display jobs for "
				                 "current user only");
  struct arg_lit *all_res  = arg_lit0(NULL,"all-reserv",
				           "show all advanced reservations"); 
  struct arg_lit *help     = arg_lit0(NULL,"help","print this help and exit");
  struct arg_lit *version  = arg_lit0(NULL,"version",
				      "print version information and exit");
  struct arg_end *end      = arg_end(20);
  void* argtable[]         = {l,u,all_res,help,version,end};
  const char* progname     = "slurm_showq";
  int exitcode=0;
  int nerrors;
#endif

  /* verify the argtable[] entries were allocated sucessfully */

  if (arg_nullcheck(argtable) != 0)
    {
      printf("%s: insufficient memory\n",progname);
      exit(1);
    }

  /* set any command line default values prior to parsing */

  /* Parse the command line as defined by argtable[] */

  nerrors = arg_parse(argc,argv,argtable);

  /* special case: '--help' takes precedence over error reporting */

  if (help->count > 0)
    {
      printf("\nUsage: %s ", progname);
      arg_print_syntax(stdout,argtable,"\n");
      printf("\nshowq summarizes all running, idle, and pending jobs \n");
      printf("along with any advanced reservations "
	     "in the SLURM batch system.\n\n");
      arg_print_glossary(stdout,argtable,"  %-25s %s\n");
      printf("\n");
      exit(0);
    }

  /* special case: '--version' takes precedence error reporting */

  if (version->count > 0)
    {
      printf("\n%s: Version %2.1f\n",progname,VERSION);
      printf("Texas Advanced Computing Center, "
	     "The University of Texas at Austin\n\n");
      exit(0);
    }

  /* If the parser returned any errors then display them and exit */

  if (nerrors > 0)
    {
      arg_print_errors(stdout,end,progname);
      printf("Try '%s --help' for more information.\n",progname);
      exit(1);
    }

  /* normal case: take the command line options at face value */

  if(l->count > 0)
    long_listing = 1;
  
  if(u->count > 0)
    user_jobs_only = 1;

  if(all_res->count > 0)
    show_all_reserv = 1;

  return;

}
#endif

char *Slurm_Showq::getusername() 
{
  char *user = 0;
  struct passwd *pw;

  pw = getpwuid(getuid());

  if (pw)
    user = pw->pw_name;
  return user;
}

std::string Slurm_Showq::uid_to_string(uid_t id)
{
  std::string username="nobody";
  struct passwd *pwd;

  static TUserCache UserUidMap;

  if ( UserUidMap.find(id) == UserUidMap.end() )
  {

    pwd = getpwuid(id);
    if (NULL != pwd)
    {
      username = pwd->pw_name;
      UserUidMap[id]=username;
    }
  }
  else
  {
    username = UserUidMap[id];
  }

  return(username);
      
}

//-------------------------------------------------------------
// parse_supported_options: Parse/Define command line arguments
//-------------------------------------------------------------

void Slurm_Showq::parse_supported_options(int argc, char *argv[])
{

  GetPot cl(argc,argv);

  if(cl.search("--version"))
    print_version();

  if(cl.search(2,"--long","-l"))
    long_listing = true;

  if(cl.search(2,"--blocked","-b"))
    show_blocked = true;

  if(cl.search(2,"--help","-h"))
    print_usage();

  if(cl.search(2,"-u","--user"))
    user_jobs_only = true;

  if (cl.search("-U"))
    {
      named_user_jobs_only=true;
      named_user=cl.next("");
    }

  if (cl.search(2,"-p","-q"))
	{
	  named_partition_jobs_only=true;
	  named_partition=cl.next("");
	}

  if (cl.search("-o"))
	{
	  sort_by_priority=true;
    }

  if (user_jobs_only && named_user_jobs_only)
    {
      fprintf(stderr,"\n -U and -u are mutually exclusive options. Exiting.\n");
      print_usage();
    }

  if (cl.search("-s"))
    {
      summary_only=true;
    }

  

}

//-------------------------------------------------------------
// print_usage: show command options
//-------------------------------------------------------------

void Slurm_Showq::print_usage()
{
  printf("\nUsage: %s [OPTIONS]\n\n", progname.c_str());
  printf("Thus utility summarizes all running, idle, and pending jobs \n");
  printf("along with any upcoming advanced reservations in the SLURM batch system.\n\n");
	 
  printf("OPTIONS:\n");
  printf("  --help                  generate help message and exit\n");
  printf("  --version               output version information and exit\n");
  printf("  --all-reserv            show all advanced reservations\n");
  printf("  -l [ --long ]           enable more verbose (long) listing\n");
  printf("  -u [ --user ]           display jobs for current user only\n");
  printf("  -U <username>           display jobs for username only\n");
  printf("  -p, -q partition        display jobs for partition/queue\n");
  printf("  -o                      display pending jobs in order of priority\n");
  printf("  -s                      display job summary only\n");
  
  printf("\n");
  exit(0);
}

//-------------------------------------------------------------
// print_version: echo current version
//-------------------------------------------------------------

void Slurm_Showq::print_version()
{
  printf("\n--------------------------------------------------\n");
  printf("%s: Version %2.1f\n\n",progname.c_str(),VERSION);
  printf("Texas Advanced Computing Center\n");
  printf("The University of Texas at Austin\n");
  printf("Research Computing\n");
  printf("Harvard University\n");
  printf("--------------------------------------------------\n");
  printf("\n");

  exit(0);

  return;
}

//-----------------------------------------------------------------
// read_runtime_config()
//----------------------------------------------------------------- 

void Slurm_Showq::read_runtime_config(std::string config_file)
{

  // determine where binary is being run from

  string exe = get_executable_path();

  char *exe_tmp = new char[exe.size() + 1];
  std::copy(exe.begin(),exe.end(),exe_tmp);

  exe_tmp[exe.size()] = '\0';

  std::string ifile(dirname(exe_tmp));

  delete [] exe_tmp;
  
  ifile += "/" + config_file;

  FILE *fp = fopen(ifile.c_str(),"r");

  if(fp == NULL)
    return;
  else
      {
	GetPot parse(ifile.c_str(),"#","\n");
	//parse.print();

	if(parse.size() <= 1)   // empty config file
	  return;		

	int num_hosts = parse("hosts_available",0);

	if(num_hosts > 0)
	  {
	    total_avail_nodes = num_hosts;
	    show_utilization = true;
	  }
      }

  return;
}

std::string Slurm_Showq::get_executable_path()
{
  char result[ PATH_MAX ];
  ssize_t count = readlink( "/proc/self/exe", result, PATH_MAX );
  return std::string( result, (count > 0) ? count : 0 );
}

//-----------------------------------------------------------------
// Slurm_Showq(): Default constructor
//----------------------------------------------------------------- 

Slurm_Showq::Slurm_Showq()
{
  // default values

  show_blocked     = false;
  long_listing     = false;
  user_jobs_only   = false;
  named_user_jobs_only = false;
  named_partition_jobs_only = false;
  show_all_reserv  = false;
  show_utilization = false;
  sort_by_priority = false;
  summary_only = false;

  RES_LEAD_WINDOW = 24*7*3600;	// 7-day lead time

  progname           = "slurm_showq";

  // read additional runtime settings from config file if present

  read_runtime_config("showq.conf");

  return;
}
