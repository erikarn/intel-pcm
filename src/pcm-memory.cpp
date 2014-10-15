/*

   Copyright (c) 2009-2012, Intel Corporation
   All rights reserved.

   Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
// written by Patrick Lu
// increased max sockets to 256 - Thomas Willhalm


/*!     \file pcm-memory.cpp
  \brief Example of using CPU counters: implements a performance counter monitoring utility for memory controller channels
  */
#define HACK_TO_REMOVE_DUPLICATE_ERROR
#include <iostream>
#ifdef _MSC_VER
#pragma warning(disable : 4996) // for sprintf
#include <windows.h>
#include "../PCM_Win/windriver.h"
#else
#include <unistd.h>
#include <signal.h>
#include <sys/time.h> // for gettimeofday()
#endif
#include <math.h>
#include <iomanip>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <assert.h>
#include "cpucounters.h"
#include "utils.h"

//Programmable iMC counter
#define READ 0
#define WRITE 1
#define PARTIAL 2
#define PCM_DELAY_DEFAULT 1.0 // in seconds
#define PCM_DELAY_MIN 0.015 // 15 milliseconds is practical on most modern CPUs
#define PCM_CALIBRATION_INTERVAL 50 // calibrate clock only every 50th iteration

using namespace std;

void print_help(const string prog_name)
{
    cerr << endl << " Usage: " << endl << " " << prog_name
         << " --help | [delay] [options] [-- external_program [external_program_options]]" << endl;
    cerr << "   <delay>                           => time interval to sample performance counters." << endl;
    cerr << "                                        If not specified, or 0, with external program given" << endl;
    cerr << "                                        will read counters only after external program finishes" << endl;
    cerr << " Supported <options> are: " << endl;
    cerr << "  -h    | --help  | /h               => print this help and exit" << endl;
    cerr << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or" << endl
         << "                                        to a file, in case filename is provided" << endl;
#ifdef _MSC_VER
    cerr << "  --uninstallDriver | --installDriver=> (un)install driver" << endl;
#endif
    cerr << " Examples:" << endl;
    cerr << "  " << prog_name << " 1                  => print counters every second without core and socket output" << endl;
    cerr << "  " << prog_name << " 0.5 -csv=test.log  => twice a second save counter values to test.log in CSV format" << endl;
    cerr << "  " << prog_name << " /csv 5 2>/dev/null => one sampe every 5 seconds, and discard all diagnostic output" << endl;
    cerr << endl;
}

void display_bandwidth(float *iMC_Rd_socket_chan, float *iMC_Wr_socket_chan, float *iMC_Rd_socket, float *iMC_Wr_socket, uint32 numSockets, uint32 num_imc_channels, uint64 *partial_write)
{
    float sysRead = 0.0, sysWrite = 0.0;
    uint32 skt = 0;
    cout.setf(ios::fixed);
    cout.precision(2);

    while(skt < numSockets)
    {
        if(!(skt % 2) && ((skt+1) < numSockets)) //This is even socket, and it has at least one more socket which can be displayed together
        {
            cout << "\
                \r---------------------------------------||---------------------------------------\n\
                \r--             Socket "<<skt<<"              --||--             Socket "<<skt+1<<"              --\n\
                \r---------------------------------------||---------------------------------------\n\
                \r---------------------------------------||---------------------------------------\n\
                \r---------------------------------------||---------------------------------------\n\
                \r--   Memory Performance Monitoring   --||--   Memory Performance Monitoring   --\n\
                \r---------------------------------------||---------------------------------------\n\
                \r"; 
            for(uint64 channel = 0; channel < num_imc_channels; ++channel)
            {
                if(iMC_Rd_socket_chan[skt*num_imc_channels+channel] < 0.0 && iMC_Wr_socket_chan[skt*num_imc_channels+channel] < 0.0) //If the channel read neg. value, the channel is not working; skip it.
                    continue;
                std::cout << "\r--  Mem Ch "
                    <<channel
                    <<": Reads (MB/s):"
                    <<setw(8)
                    <<iMC_Rd_socket_chan[skt*num_imc_channels+channel];
                cout <<"  --||--  Mem Ch "
                    <<channel
                    <<": Reads (MB/s):"
                    <<setw(8)
                    <<iMC_Rd_socket_chan[(skt+1)*num_imc_channels+channel]
                    <<"  --"
                    <<endl;
                cout << "\r--            Writes(MB/s):"
                    <<setw(8)
                    <<iMC_Wr_socket_chan[skt*num_imc_channels+channel];
                cout <<"  --||--            Writes(MB/s):"
                    <<setw(8)
                    <<iMC_Wr_socket_chan[(skt+1)*num_imc_channels+channel]
                    <<"  --"
                    <<endl;
            }
            cout << "\
                \r-- NODE"<<skt<<" Mem Read (MB/s):  "<<setw(8)<<iMC_Rd_socket[skt]<<"  --||-- NODE"<<skt+1<<" Mem Read (MB/s):  "<<setw(8)<<iMC_Rd_socket[skt+1]<<"  --\n\
                \r-- NODE"<<skt<<" Mem Write (MB/s): "<<setw(8)<<iMC_Wr_socket[skt]<<"  --||-- NODE"<<skt+1<<" Mem Write (MB/s): "<<setw(8)<<iMC_Wr_socket[skt+1]<<"  --\n\
                \r-- NODE"<<skt<<" P. Write (T/s) :"<<dec<<setw(10)<<partial_write[skt]<<"  --||-- NODE"<<skt+1<<" P. Write (T/s): "<<dec<<setw(10)<<partial_write[skt+1]<<"  --\n\
                \r-- NODE"<<skt<<" Memory (MB/s): "<<setw(11)<<std::right<<iMC_Rd_socket[skt]+iMC_Wr_socket[skt]<<"  --||-- NODE"<<skt+1<<" Memory (MB/s): "<<setw(11)<<iMC_Rd_socket[skt+1]+iMC_Wr_socket[skt+1]<<"  --\n\
                \r";
           sysRead += iMC_Rd_socket[skt];
           sysRead += iMC_Rd_socket[skt+1];
           sysWrite += iMC_Wr_socket[skt];
           sysWrite += iMC_Wr_socket[skt+1];
           skt += 2;
        }
        else //Display one socket in this row
        {
            cout << "\
                \r---------------------------------------|\n\
                \r--             Socket "<<skt<<"              --|\n\
                \r---------------------------------------|\n\
                \r---------------------------------------|\n\
                \r---------------------------------------|\n\
                \r--   Memory Performance Monitoring   --|\n\
                \r---------------------------------------|\n\
                \r"; 
            for(uint64 channel = 0; channel < num_imc_channels; ++channel)
            {
                if(iMC_Rd_socket_chan[skt*num_imc_channels+channel] < 0.0 && iMC_Wr_socket_chan[skt*num_imc_channels+channel] < 0.0) //If the channel read neg. value, the channel is not working; skip it.
                    continue;
                cout << "--  Mem Ch "
                    <<channel
                    <<": Reads (MB/s):"
                    <<setw(8)
                    <<iMC_Rd_socket_chan[skt*num_imc_channels+channel]
                    <<"  --|\n--            Writes(MB/s):"
                    <<setw(8)
                    <<iMC_Wr_socket_chan[skt*num_imc_channels+channel]
                    <<"  --|\n";

            }
            cout << "\
                \r-- NODE"<<skt<<" Mem Read (MB/s):  "<<setw(8)<<iMC_Rd_socket[skt]<<"  --|\n\
                \r-- NODE"<<skt<<" Mem Write (MB/s) :"<<setw(8)<<iMC_Wr_socket[skt]<<"  --|\n\
                \r-- NODE"<<skt<<" P. Write (T/s) :"<<setw(10)<<dec<<partial_write[skt]<<"  --|\n\
                \r-- NODE"<<skt<<" Memory (MB/s): "<<setw(8)<<iMC_Rd_socket[skt]+iMC_Wr_socket[skt]<<"     --|\n\
                \r";

            sysRead += iMC_Rd_socket[skt];
            sysWrite += iMC_Wr_socket[skt];
            skt += 1;
        }
    }
    cout << "\
        \r---------------------------------------||---------------------------------------\n\
        \r--                   System Read Throughput(MB/s):"<<setw(10)<<sysRead<<"                  --\n\
        \r--                  System Write Throughput(MB/s):"<<setw(10)<<sysWrite<<"                  --\n\
        \r--                 System Memory Throughput(MB/s):"<<setw(10)<<sysRead+sysWrite<<"                  --\n\
        \r---------------------------------------||---------------------------------------" << endl;
}

void display_bandwidth_csv_header(float *iMC_Rd_socket_chan, float *iMC_Wr_socket_chan, float *iMC_Rd_socket, float *iMC_Wr_socket, uint32 numSockets, uint32 num_imc_channels, uint64 *partial_write)
{
  cout << ";;" ; // Time

    for (uint32 skt=0; skt < numSockets; ++skt)
    {
      for(uint64 channel = 0; channel < num_imc_channels; ++channel)
	{
	  if(iMC_Rd_socket_chan[skt*num_imc_channels+channel] < 0.0 && iMC_Wr_socket_chan[skt*num_imc_channels+channel] < 0.0) //If the channel read neg. value, the channel is not working; skip it.
	    continue;
	  cout << "SKT" << skt << ";SKT" << skt << ';';
	}
      cout << "SKT"<<skt<<";"
	   << "SKT"<<skt<<";"
	   << "SKT"<<skt<<";"
	   << "SKT"<<skt<<";";
      
    }
    cout << "System;System;System\n";
      

  cout << "Date;Time;" ;
    for (uint32 skt=0; skt < numSockets; ++skt)
    {
      for(uint64 channel = 0; channel < num_imc_channels; ++channel)
	{
	  if(iMC_Rd_socket_chan[skt*num_imc_channels+channel] < 0.0 && iMC_Wr_socket_chan[skt*num_imc_channels+channel] < 0.0) //If the channel read neg. value, the channel is not working; skip it.
	    continue;
	  cout << "Ch" <<channel <<"Read;"
	       << "Ch" <<channel <<"Write;";
	}
      cout << "Mem Read (MB/s);Mem Write (MB/s); P. Write (T/s); Memory (MB/s);";
    }

    cout << "Read;Write;Memory" << endl;
}

void display_bandwidth_csv(float *iMC_Rd_socket_chan, float *iMC_Wr_socket_chan, float *iMC_Rd_socket, float *iMC_Wr_socket, uint32 numSockets, uint32 num_imc_channels, uint64 *partial_write, uint64 elapsedTime)
{
    time_t t = time(NULL);
    tm *tt = localtime(&t);
    cout.precision(3);
    cout << 1900+tt->tm_year << '-' << 1+tt->tm_mon << '-' << tt->tm_mday << ';'
         << tt->tm_hour << ':' << tt->tm_min << ':' << tt->tm_sec << ';';


    float sysRead = 0.0, sysWrite = 0.0;

    cout.setf(ios::fixed);
    cout.precision(2);

    for (uint32 skt=0; skt < numSockets; ++skt)
     {
       for(uint64 channel = 0; channel < num_imc_channels; ++channel)
	 {
	   if(iMC_Rd_socket_chan[skt*num_imc_channels+channel] < 0.0 && iMC_Wr_socket_chan[skt*num_imc_channels+channel] < 0.0) //If the channel read neg. value, the channel is not working; skip it.
	     continue;
	   cout <<setw(8) <<iMC_Rd_socket_chan[skt*num_imc_channels+channel] << ';'
		<<setw(8) <<iMC_Wr_socket_chan[skt*num_imc_channels+channel] << ';';
	   
	 }
       cout <<setw(8) <<iMC_Rd_socket[skt] <<';'
            <<setw(8) <<iMC_Wr_socket[skt] <<';'
            <<setw(10) <<dec<<partial_write[skt] <<';'
            <<setw(8) <<iMC_Rd_socket[skt]+iMC_Wr_socket[skt] <<';';

            sysRead += iMC_Rd_socket[skt];
            sysWrite += iMC_Wr_socket[skt];
    }

    cout <<setw(10) <<sysRead <<';'
	 <<setw(10) <<sysWrite <<';'
	 <<setw(10) <<sysRead+sysWrite << endl;
}

const uint32 max_sockets = 256;
const uint32 max_imc_channels = 8;

void calculate_bandwidth(PCM *m, const ServerUncorePowerState uncState1[], const ServerUncorePowerState uncState2[], uint64 elapsedTime, bool csv, bool & csvheader)
{
    //const uint32 num_imc_channels = m->getMCChannelsPerSocket();
    float iMC_Rd_socket_chan[max_sockets][max_imc_channels];
    float iMC_Wr_socket_chan[max_sockets][max_imc_channels];
    float iMC_Rd_socket[max_sockets];
    float iMC_Wr_socket[max_sockets];
    uint64 partial_write[max_sockets];

    for(uint32 skt = 0; skt < m->getNumSockets(); ++skt)
    {
        iMC_Rd_socket[skt] = 0.0;
        iMC_Wr_socket[skt] = 0.0;
        partial_write[skt] = 0;

        for(uint32 channel = 0; channel < max_imc_channels; ++channel)
        {
            if(getMCCounter(channel,READ,uncState1[skt],uncState2[skt]) == 0.0 && getMCCounter(channel,WRITE,uncState1[skt],uncState2[skt]) == 0.0) //In case of JKT-EN, there are only three channels. Skip one and continue.
            {
                iMC_Rd_socket_chan[skt][channel] = -1.0;
                iMC_Wr_socket_chan[skt][channel] = -1.0;
                continue;
            }

            iMC_Rd_socket_chan[skt][channel] = (float) (getMCCounter(channel,READ,uncState1[skt],uncState2[skt]) * 64 / 1000000.0 / (elapsedTime/1000.0));
            iMC_Wr_socket_chan[skt][channel] = (float) (getMCCounter(channel,WRITE,uncState1[skt],uncState2[skt]) * 64 / 1000000.0 / (elapsedTime/1000.0));

            iMC_Rd_socket[skt] += iMC_Rd_socket_chan[skt][channel];
            iMC_Wr_socket[skt] += iMC_Wr_socket_chan[skt][channel];

            partial_write[skt] += (uint64) (getMCCounter(channel,PARTIAL,uncState1[skt],uncState2[skt]) / (elapsedTime/1000.0));
        }
    }

    if (csv) {
      if (csvheader) {
	display_bandwidth_csv_header(iMC_Rd_socket_chan[0], iMC_Wr_socket_chan[0], iMC_Rd_socket, iMC_Wr_socket, m->getNumSockets(), max_imc_channels, partial_write);
	csvheader = false;
      }
      display_bandwidth_csv(iMC_Rd_socket_chan[0], iMC_Wr_socket_chan[0], iMC_Rd_socket, iMC_Wr_socket, m->getNumSockets(), max_imc_channels, partial_write, elapsedTime);
    } else {
      display_bandwidth(iMC_Rd_socket_chan[0], iMC_Wr_socket_chan[0], iMC_Rd_socket, iMC_Wr_socket, m->getNumSockets(), max_imc_channels, partial_write);
    }
}

int main(int argc, char * argv[])
{
    set_signal_handlers();

#ifdef PCM_FORCE_SILENT
    null_stream nullStream1, nullStream2;
    std::cout.rdbuf(&nullStream1);
    std::cerr.rdbuf(&nullStream2);
#endif

#ifdef _MSC_VER
    TCHAR driverPath[1040]; // length for current directory + "\\msr.sys"
    GetCurrentDirectory(1024, driverPath);
    wcscat_s(driverPath, 1040, L"\\msr.sys");
#endif

    cerr << endl;
    cerr << " Intel(r) Performance Counter Monitor: Memory Bandwidth Monitoring Utility " << INTEL_PCM_VERSION << endl;
    cerr << endl;
    cerr << " Copyright (c) 2009-2014 Intel Corporation" << endl;
    cerr << " This utility measures memory bandwidth per channel in real-time" << endl;
    cerr << endl;

    double delay = -1.0;
    bool csv = false, csvheader=false;
    char * sysCmd = NULL;
    char ** sysArgv = NULL;
    long diff_usec = 0; // deviation of clock is useconds between measurements
    int calibrated = PCM_CALIBRATION_INTERVAL - 2; // keeps track is the clock calibration needed
    string program = string(argv[0]);

    PCM * m = PCM::getInstance();

    if(argc > 1) do
    {
        argv++;
        argc--;
        if (strncmp(*argv, "--help", 6) == 0 ||
            strncmp(*argv, "-h", 2) == 0 ||
            strncmp(*argv, "/h", 2) == 0)
        {
            print_help(program);
            exit(EXIT_FAILURE);
        }
        else
        if (strncmp(*argv, "-csv",4) == 0 ||
            strncmp(*argv, "/csv",4) == 0)
        {
            csv = true;
			csvheader = true;
            string cmd = string(*argv);
            size_t found = cmd.find('=',4);
            if (found != string::npos) {
                string filename = cmd.substr(found+1);
                if (!filename.empty()) {
                    m->setOutput(filename);
                }
            }
            continue;
        }
#ifdef _MSC_VER
        else
        if (strncmp(*argv, "--uninstallDriver", 17) == 0)
        {
            Driver tmpDrvObject;
            tmpDrvObject.uninstall();
            cerr << "msr.sys driver has been uninstalled. You might need to reboot the system to make this effective." << endl;
            exit(EXIT_SUCCESS);
        }
        else
        if (strncmp(*argv, "--installDriver", 15) == 0)
        {
            Driver tmpDrvObject;
            if (!tmpDrvObject.start(driverPath))
            {
                cerr << "Can not access CPU counters" << endl;
                cerr << "You must have signed msr.sys driver in your current directory and have administrator rights to run this program" << endl;
                exit(EXIT_FAILURE);
            }
            exit(EXIT_SUCCESS);
        }
#endif
        else
        if (strncmp(*argv, "--", 2) == 0)
        {
            argv++;
            sysCmd = *argv;
            sysArgv = argv;
            break;
        }
        else
        {
            // any other options positional that is a floating point number is treated as <delay>,
            // while the other options are ignored with a warning issues to stderr
            double delay_input;
            std::istringstream is_str_stream(*argv);
            is_str_stream >> noskipws >> delay_input;
            if(is_str_stream.eof() && !is_str_stream.fail()) {
                delay = delay_input;
            } else {
                cerr << "WARNING: unknown command-line option: \"" << *argv << "\". Ignoring it." << endl;
                print_help(program);
                exit(EXIT_FAILURE);
            }
            continue;
        }
    } while(argc > 1); // end of command line partsing loop

    m->disableJKTWorkaround();
    PCM::ErrorCode status = m->program();
    switch (status)
    {
        case PCM::Success:
            break;
        case PCM::MSRAccessDenied:
            cerr << "Access to Intel(r) Performance Counter Monitor has denied (no MSR or PCI CFG space access)." << endl;
            exit(EXIT_FAILURE);
        case PCM::PMUBusy:
            cerr << "Access to Intel(r) Performance Counter Monitor has denied (Performance Monitoring Unit is occupied by other application). Try to stop the application that uses PMU." << endl;
            cerr << "Alternatively you can try to reset PMU configuration at your own risk. Try to reset? (y/n)" << endl;
            char yn;
            std::cin >> yn;
            if ('y' == yn)
            {
                m->resetPMU();
                cerr << "PMU configuration has been reset. Try to rerun the program again." << endl;
            }
            exit(EXIT_FAILURE);
        default:
            cerr << "Access to Intel(r) Performance Counter Monitor has denied (Unknown error)." << endl;
            exit(EXIT_FAILURE);
    }
    
    cerr << "\nDetected "<< m->getCPUBrandString() << " \"Intel(r) microarchitecture codename "<<m->getUArchCodename()<<"\""<<endl;
    if(!m->hasPCICFGUncore())
    {
        cerr << "Jaketown, Ivytown or Haswell Server CPU is required for this tool!" << endl;
        if(m->memoryTrafficMetricsAvailable())
            cerr << "For processor-level memory bandwidth statistics please use pcm.x" << endl;
        exit(EXIT_FAILURE);
    }

    if(m->getNumSockets() > max_sockets)
    {
        cerr << "Only systems with up to "<<max_sockets<<" sockets are supported! Program aborted" << endl;
        exit(EXIT_FAILURE);
    }

    ServerUncorePowerState * BeforeState = new ServerUncorePowerState[m->getNumSockets()];
    ServerUncorePowerState * AfterState = new ServerUncorePowerState[m->getNumSockets()];
    uint64 BeforeTime = 0, AfterTime = 0;

    if ( (sysCmd != NULL) && (delay<=0.0) ) {
        // in case external command is provided in command line, and
        // delay either not provided (-1) or is zero
        m->setBlocked(true);
    } else {
        m->setBlocked(false);
    }

    if (csv) {
        if( delay<=0.0 ) delay = PCM_DELAY_DEFAULT;
    } else {
        // for non-CSV mode delay < 1.0 does not make a lot of practical sense: 
        // hard to read from the screen, or
        // in case delay is not provided in command line => set default
        if( ((delay<1.0) && (delay>0.0)) || (delay<=0.0) ) delay = PCM_DELAY_DEFAULT;
    }

    cerr << "Update every "<<delay<<" seconds"<< endl;

    for(uint32 i=0; i<m->getNumSockets(); ++i)
        BeforeState[i] = m->getServerUncorePowerState(i); 

    BeforeTime = m->getTickCount();

    if( sysCmd != NULL ) {
        MySystem(sysCmd, sysArgv);
    }

    while(1)
    {
        if(!csv) cout << std::flush;
        int delay_ms = int(delay * 1000);
        int calibrated_delay_ms = delay_ms;
#ifdef _MSC_VER
        // compensate slow Windows console output
        if(AfterTime) delay_ms -= (int)(m->getTickCount() - BeforeTime);
        if(delay_ms < 0) delay_ms = 0;
#else
        // compensation of delay on Linux/UNIX
        // to make the samling interval as monotone as possible
        struct timeval start_ts, end_ts;
        if(calibrated == 0) {
            gettimeofday(&end_ts, NULL);
            diff_usec = (end_ts.tv_sec-start_ts.tv_sec)*1000000.0+(end_ts.tv_usec-start_ts.tv_usec);
            calibrated_delay_ms = delay_ms - diff_usec/1000.0;
        }
#endif

        MySleepMs(calibrated_delay_ms);

#ifndef _MSC_VER
        calibrated = (calibrated + 1) % PCM_CALIBRATION_INTERVAL;
        if(calibrated == 0) {
            gettimeofday(&start_ts, NULL);
        }
#endif

        AfterTime = m->getTickCount();
        for(uint32 i=0; i<m->getNumSockets(); ++i)
            AfterState[i] = m->getServerUncorePowerState(i);

	if (!csv) {
	  cout << "Time elapsed: "<<dec<<fixed<<AfterTime-BeforeTime<<" ms\n";
	  cout << "Called sleep function for "<<dec<<fixed<<delay_ms<<" ms\n";
	}

        calculate_bandwidth(m,BeforeState,AfterState,AfterTime-BeforeTime,csv,csvheader);

        swap(BeforeTime, AfterTime);
        swap(BeforeState, AfterState);

        if ( m->isBlocked() ) {
        // in case PCM was blocked after spawning child application: break monitoring loop here
            break;
        }
    }

    delete[] BeforeState;
    delete[] AfterState;

    exit(EXIT_SUCCESS);
}
