/*
e.g. :
g++ -O3 -std=c++17 -pthread fsscanner_atomic_find_grep.cpp -o fsscanner_atomic_find_grep && time ./fsscanner_atomic_find_grep 41 /usr/src/linux  "MAINTAINERS" grep

avx2:
g++ -DUSEAVX2 -mavx2 -O3 -std=c++17 -pthread fsscanner_atomic_find_grep.cpp -o fsscanner_atomic_find_grep

avx2 with Intel Compiler:
dpcpp -DUSEAVX2 -mavx2 -O3 -std=c++17 -pthread fsscanner_atomic_find_grep.cpp -o fsscanner_atomic_find_grep && time ./fsscanner_atomic_find_grep 2 ~/nvme/code MAINTAINERS grep

concurrent find respectively concurrent grep
usage: ./fsscanner_atomic_find_grep <number of threads> <path> <search_string> [grep|find] 
defaults to find



notes:

also searching in file path part not just after last  more like find -type f <path> | grep <search_string>

 if (too many atomics): SLOW, else: same speed

- g++ -O3 -std=c++17 -pthread fsscanner_atomic_find_grep.cpp -o fsscanner_atomic_find_grep && time sudo ./fsscanner_atomic_find_grep 64 / "some_file" find 

----
heuristic: array of atomic variables are used to synchronize/wait for other threads which might find new directories
- increment atomic variable in section where we do directory listings
- decrement after we are done in this section
- check global vector with directories for more work
- wait if other threads have a postive atomic variable
- else: die
----

# for more open files
ulimit -n 123456
# for more threads > 32k; I guess 2 mmap's per thread
echo 777888 >  /proc/sys/vm/max_map_count

e.g. use `iostat -x -p nvme0n1p1 --human 1` to find sweet spot
mpstat -P ALL 1
vmstat 1
vmstat -m 1  | egrep "dentry|inode_cache" # print slabs

bcc/tools/syscount.py to monitor syscalls


read-ahead settings, scheduler, etc: https://cromwell-intl.com/open-source/performance-tuning/disks.html
https://wiki.mikejung.biz/Ubuntu_Performance_Tuning

echo kyber > /sys/block/sda/queue/scheduler
echo 512 > /sys/block/sda/queue/nr_requests
echo 0 > /sys/block/sda/queue/read_ahead_kb

echo kyber > /sys/block/nvme0n1/queue/scheduler
echo 256 > /sys/block/nvme0n1/queue/nr_requests
echo 0 > /sys/block/nvme0n1/queue/read_ahead_kb

test filesystem on samsung nvme:
128.000 directories
1.250.000 files 250.000 binary
209GB
find:
$ time find /nvme -name "*MAINTAINERS*"
15 secs; in-cache: 1.8s
$ fsscanner 32 threads
1 sec; in-cache: 0.25s
$ fsscanner 128 threads
0.6 sec

$ grep 

$ fsscanner 12 /nvme "MAINTAINERS" grep
29
$ fsscanner 20 /nvme "MAINTAINERS" grep
23 sec
$ fsscanner 128 /nvme "MAINTAINERS" grep
16-20 sec min: with 209 threads: 11 sec

$ time (find /nvme/bm/code/ -type f -print0  | xargs -P8 -0 grep MAINTAINERS | wc -l)
 60 sec
$ time (find /nvme/bm/code/ -type f -print0  | xargs -P64 -0 grep MAINTAINERS | wc -l) 
 45 sec
$ time (find /nvme/bm/code/ -type f -print0  | xargs -P80 -0 grep MAINTAINERS | wc -l)
 45 sec

time (find /nvme/bm/code/ -type f -print0  | xargs -P64 -0 grep -I MAINTAINERS | wc -l)
 20 sec


*/

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <locale>
#include <chrono>
#include <filesystem>
#include <functional>  // std::ref
#include <iostream>
#include <fstream>
#include <thread>
#include <array>
#include <vector>
//#include <unordered_map>
//#include <condition_variable>
#include <mutex>
#include <exception>
#include <cstdlib>
#include <dirent.h> 
#include <sys/stat.h>
#include <chrono>
#include <typeinfo>
#include <atomic>
#include <ctime>
#include <new>
//#define DEBUG 0

// avx2 strstr:
#ifdef USEAVX2
#include <immintrin.h>
#include <cassert>
#include "avx2/bits.cpp"
#include "avx2/avx2.cpp"
#include "avx2/common.h"
#include "avx2/fixed-memcmp.cpp"
#include "avx2/avx2-strstr-v2.cpp"
#endif
// 

thread_local std::string tls_path;
thread_local std::vector<std::string> tls_filenames;
thread_local std::vector<std::string> tls_directories;

#define NR_GLOBAL_FILENAME_MUTEXES 8 // also works surprisingly well with 1
std::array<std::mutex, NR_GLOBAL_FILENAME_MUTEXES> global_filename_mutexes; // mutexes for the following:
std::array<std::vector<std::string>, NR_GLOBAL_FILENAME_MUTEXES> filename_array_of_vectors;

#define NR_GLOBAL_DIRECTORIES_MUTEXES 8 // also works surprisingly well with 1
std::array<std::mutex, NR_GLOBAL_DIRECTORIES_MUTEXES> global_directories_mutexes; // mutexes for the following:
std::array<std::vector<std::string>, NR_GLOBAL_DIRECTORIES_MUTEXES> directories_array_of_vectors;

std::vector<std::string> global_filenames(0);
std::vector<std::string> global_directories(0);
std::vector<std::exception_ptr> global_exceptions;
std::mutex coutmtx, global_exceptmutex; // mutex for std::cout and for exceptions thrown in threads

#define NR_ATOMICS 8
std::array<std::atomic<int>, NR_ATOMICS> atomic_running_threads;


std::vector<uint64_t> running_times;
std::mutex running_times_mutex;

std::vector<uint64_t> starting_times;
std::mutex starting_times_mutex;
std::string searching_for{};
std::string search_mode{"find"}; // "find" or "grep"
uint64_t hits{0};                                  

std::atomic<long long> sum_file_size{0};
//std::chrono::time_point starting_time;

class Worker {
public:
    Worker(int n, std::string s): worker_id(n), start_with_path(s) {
        if(worker_id == -42) // temp worker for startup
            //buffer = (char*)malloc(buffer_size);
            buffer = (char*)memalign(4096, buffer_size );
    }
    ~Worker(){ if(worker_id == -42){ free(buffer); }  }
    void operator()(){
        try{
            //buffer = (char*)malloc(buffer_size);
            buffer = (char*)memalign(4096, buffer_size);

            if(start_with_path != ""){
                tls_path=start_with_path;
                do_linear_descent();
                #ifndef SLEEP                
                std::this_thread::sleep_for(std::chrono::milliseconds(0 + worker_id%8)); 
                #endif  
                working();  
            } else {
                #ifdef DEBUG                
                {std::lock_guard<std::mutex> lock(coutmtx);
                 std::cout << worker_id << ": EMPTY constructor string." << std::endl;}
                #endif            
                //#ifndef DONTSLEEP                
                std::this_thread::sleep_for(std::chrono::milliseconds(1 + worker_id%7));     
                //#endif                    
                working(); 
            }

            free(buffer);

        } catch(...){
            std::lock_guard<std::mutex> lock(global_exceptmutex);
            global_exceptions.push_back(std::current_exception());
        }
    }

    void working(){
        #ifdef DIE_DEBUG
        auto starting_time = std::chrono::high_resolution_clock::now();
        uint64_t us = std::chrono::duration_cast<std::chrono::microseconds>(starting_time.time_since_epoch()).count();
        #endif
        
        #ifdef DEBUG
        {std::lock_guard<std::mutex> lock(coutmtx);
        std::cout << worker_id << ": constructor string: " << tls_path << std::endl;}
        #endif
        #define RETRY_COUNT 0 // a few or a few dozen works equally well (for me)
        int retry_count = RETRY_COUNT;
        int dont_wait_forever = 111;
    check_again:
        int proceed_new=0;
        #define NR_OF_ROUNDS 1 // a few or just one round works equally well
        for(int i=0; proceed_new == 0 && i < NR_OF_ROUNDS*NR_GLOBAL_DIRECTORIES_MUTEXES; i++){  
            int start_with_i=(worker_id+i)%NR_GLOBAL_DIRECTORIES_MUTEXES; // start with own id-vector
            std::lock_guard<std::mutex> lock(global_directories_mutexes[start_with_i]);
            if(!directories_array_of_vectors[start_with_i].empty()){
                tls_path=directories_array_of_vectors[start_with_i].back();
                directories_array_of_vectors[start_with_i].pop_back();
                proceed_new=1;
                break;
            }
            //std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
        if(proceed_new == 1){ // equal to if(tls_path != "")
            #ifdef DIE_DEBUG
            if(retry_count != RETRY_COUNT){
                std::lock_guard<std::mutex> lock(coutmtx);
                std::cout << "worker: " << worker_id << " did reset RETRY_COUNT" << std::endl;
            }
            #endif
            retry_count=RETRY_COUNT; // reset retry_count
            do_linear_descent();
        }
        else{
            #ifdef DIE_DEBUG2
            {std::lock_guard<std::mutex> lock(coutmtx);
             std::cout << "worker: " << worker_id << " is about to die at retry-lives-left:# " << retry_count << " did not find work in directories_array_of_vectors" << std::endl;
            }
            #endif
            goto end_label; // thread will die here eventually
        }  

        goto check_again; // after returning from do_linear_descent() in if(proceed_new==1) above, check if there is more work

    end_label:  
        if(retry_count-- > 0){
            std::this_thread::sleep_for(std::chrono::microseconds( 510 ));
            goto check_again;
        }
        else { 
            for(int i=0; i<NR_ATOMICS; i++){
                //std::cout << "zzzzz [" << i << "] running" << std::endl;
                if(atomic_running_threads[i] > 0){
                    std::this_thread::sleep_for(std::chrono::microseconds(517));
                    if(dont_wait_forever-- > 0){ // protection for way too many threads
                        goto check_again;
                    } else{
                        break;
                    }
                }
            }
            #ifdef DIE_DEBUG
             auto end_time = std::chrono::high_resolution_clock::now();
             std::chrono::nanoseconds ns = end_time-starting_time;
             auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(ns).count();
             {std::lock_guard<std::mutex> lock(coutmtx);
             std::cout << "worker: " << worker_id << " FINALLY DIED after: " << elapsed << " microseconds " << elapsed/1000 << " millisecs " << elapsed/1000000 << " secs" << std::endl;
             }
            #endif

            //auto end_time = std::chrono::high_resolution_clock::now();
            //std::chrono::nanoseconds ns = end_time-starting_time;
            //auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(ns).count();
            //
            //{ std::lock_guard<std::mutex> lock(running_times_mutex);
            //running_times.push_back( static_cast<uint64_t>(elapsed) );
            //}
            
            return; 
        }
                    
    }

/*
 descent into ONE subdirectory found by `do_tree_walking()` linearly,
 meaning: step into ONE directory after another, till the end of this branch of the directory tree
*/

    void do_linear_descent() {

        // increase atomic counter
        atomic_running_threads[worker_id % NR_ATOMICS]++;
        
        do {
            #ifdef DEBUG
            if(tls_path == ""){
                    std::lock_guard<std::mutex> lock(coutmtx);
                    std::cout << worker_id << ": ERROR tls_path " << tls_path << std::endl;
            }
            #endif             

            do_dir_walking(tls_path);
           
            // get one directory we found
            if (!tls_directories.empty()) {
                tls_path = tls_directories.back();
                #ifdef DEBUG          
                {std::lock_guard<std::mutex> lock(coutmtx);    
                if(tls_path == ""){ std::cout << worker_id << ": ERROR tls_path in do_linear_descent()" << std::endl; }
                }                
                #endif                
                tls_directories.pop_back();
            }
            else {
                tls_path=""; // don't walk into same dir again
            }
            // save filenames to global vector specific to this [thread_id % NR_MUTEXES]
            {
                std::lock_guard<std::mutex> lock(global_filename_mutexes[worker_id%NR_GLOBAL_FILENAME_MUTEXES]);
                for(auto const &v: tls_filenames){
                    filename_array_of_vectors[worker_id%NR_GLOBAL_FILENAME_MUTEXES].emplace_back(v);
                }
            }

            tls_filenames.clear();
            // save N-1 directories to global for other threads to pick up
            {
                std::lock_guard<std::mutex> lock(global_directories_mutexes[worker_id%NR_GLOBAL_DIRECTORIES_MUTEXES]);
                for(auto const &v: tls_directories){
                    directories_array_of_vectors[worker_id%NR_GLOBAL_DIRECTORIES_MUTEXES].emplace_back(v);
                }
            }
            tls_directories.clear();

        } while (tls_path != "");

        // decrease atomic counter
        atomic_running_threads[worker_id % NR_ATOMICS]--;
    }

    /* Replace all NUL bytes in buffer P (which ends at LIM) with EOL.
   This avoids running out of memory when binary input contains a long
   sequence of zeros, which would otherwise be considered to be part
   of a long line.  P[LIM] should be EOL.  from grep.c*/

    bool file_must_have_nulls(size_t size, int fd) {
        off_t cur = size;
        cur = lseek(fd, 0, SEEK_CUR);
        if (cur < 0)
            return false;

        /* Look for a hole after the current location.  */
        off_t hole_start = lseek(fd, cur, SEEK_HOLE);
        struct stat st;
        fstat(fd, &st);
        if (0 <= hole_start) {
            if (lseek(fd, cur, SEEK_SET) < 0)
                printf("lseek error\n");
            if (hole_start < st.st_size)
                return true;
        }
        return false;
    }

    void zap_nuls(char *p, char *lim, char eol) {  // also from grep.c
        if (eol)
            while (true) {
                *lim = '\0';
                p += strlen(p);
                *lim = eol;
                if (p == lim)
                    break;
                do
                    *p++ = eol;
                while (!*p);
            }
    }

    int find_or_grep(const char *filename) {
        int hash = -1;
        if (search_mode == "grep" || search_mode == "grep-binary") {
            // TEST:
            // diff <(awk -F\; '{print $2}' output|sort) <(grep -rI Paraná --color=never -l /nvme/bm/code | sort)
            
            char *buffer_old = NULL;
            #define SAVED_BUFFER_SIZE 256
            #define SAVED_BUFFER_HALF_SIZE 128   // save HALF_SIZE bytes from the end of the current buffer 
            char saved_buffer[SAVED_BUFFER_SIZE]; // to check for matches overlapping consec. buffers
            const char *search_string = searching_for.c_str();

            //int fd = open(filename, O_RDONLY | O_DIRECT); //O_DIRECT also works use memalign above
            int fd = open(filename, O_RDONLY);
            if (fd < 0)
                std::cout << "open ERROR: " << filename << std::endl;
            if (fd > 0) {
                int check_saved_buffer = 0;
                bool found_string = false;
                //  struct stat st;
                //  fstat(fd, &st);
                //  sum_file_size += st.st_size;
                while (int n = read(fd, buffer, read_size)) {
                    void *position;
                    if (search_mode == "grep-binary")
                        position = NULL;  // for binary search
                    else
                        position = memchr(buffer, '\0', n);  // skip binary files

                    if (position) {
                        hash = -1;
                        //  sum_file_size -= st.st_size;
                        break;
                    } else {
                        if (search_mode == "grep-binary")
                            zap_nuls(buffer, &buffer[read_size], '\n');
                        buffer[n] = '\0';
                        // buffer-corner-cases: overlapping at the end
                        if (check_saved_buffer) {
                            check_saved_buffer = 0;
                            if ((searching_for.length() > 1) && (n > 0)) {
                                int min = n < searching_for.length() - 1 ? n : searching_for.length() - 1;
                                //for (int i = 0; i < min; i++) {
                                //    saved_buffer[SAVED_BUFFER_HALF_SIZE + i] = buffer[i];
                                //}
                                memcpy(saved_buffer+SAVED_BUFFER_HALF_SIZE, buffer, min);
                                saved_buffer[SAVED_BUFFER_HALF_SIZE + min] = '\0';
                                char *saved_buffer_start = saved_buffer + SAVED_BUFFER_HALF_SIZE - 1 - (searching_for.length() - 2);
                                char *pos = strstr(saved_buffer_start, search_string);
                                if (pos) {
                                    hits++;
                                    // find last/next newline in saved_buffer print in-between
                                    printf("CORNER CASE: %s: %s\n", filename, saved_buffer);
                                    hash = 272727;
                                    
                                }
                            }
                        }

                        if (n == read_size && read_size >= SAVED_BUFFER_HALF_SIZE) { //which means buffer was full and at least 512bytes
                            check_saved_buffer = 1;
                            //for (int i = 0; i < SAVED_BUFFER_HALF_SIZE; i++) {
                            //    saved_buffer[i] = buffer[read_size - SAVED_BUFFER_HALF_SIZE+ i];
                            //}
                            memcpy(saved_buffer, buffer+read_size-SAVED_BUFFER_HALF_SIZE, SAVED_BUFFER_HALF_SIZE);
                        }
                        // end buffer-corner-cases

                        //char * pos = strstr(buffer, "Paraná");
                        char *ptr_to_next_pos = NULL;
                        char *bckp_buffer = buffer;
                        char *pos;
                        
                        //if (index == std::string::npos){ pos = NULL; }
                        //else {
                        //    pos = buffer + index;
                        //}

                        #ifdef USEAVX2
                        ssize_t index;
                        int remaining = n;
                        while ( (index=avx2_strstr_v2(bckp_buffer, remaining, search_string, searching_for.length())) != std::string::npos ) {
                            pos = bckp_buffer + index;
                            hash = 1;
                            hits++;
                            int idx_of_string = pos - buffer;
                            remaining = n - idx_of_string;
                            void *newline_before = memrchr(buffer, '\n',  idx_of_string + 1 );  // from buffer to end and reverse!
                            if (!newline_before) {
                                newline_before = buffer - 1;
                            }
                            int rest = n - idx_of_string;
                            void *newline_after = memchr(pos, '\n', rest);
                            if (!newline_after) {
                                newline_after = buffer + n ;  // be careful: read reads bufsize-1 bytes
                            }
                            int line_size = (char *)newline_after - (char *)newline_before;
                            char * sptr = (char *)newline_before + 1 ;
                            int maxlen = line_size - 1;
                            if(maxlen>80)
                                maxlen=80;
                            { std::lock_guard<std::mutex> lock(coutmtx);
                            printf("%s: %.*s\n", filename, maxlen, sptr );
                            }
                            ptr_to_next_pos = pos + searching_for.length();
                            if (ptr_to_next_pos < buffer + n){
                                bckp_buffer = ptr_to_next_pos;
                            } else {
                                break;
                            }
                            
                        }  

                        #endif
                        #ifndef USEAVX2     // NOT CUSTOM AVX2
                        
                        while ( (pos = strstr(bckp_buffer, search_string))) {
                            
                            if (hash != 272727) {  // keep the 272727 hash to count corner-cases
                                hash = 1;
                            }
                            hits++;
                            int idx_of_string = pos - buffer;
                            void *newline_before = memrchr(buffer, '\n', idx_of_string + 1);  // from buffer to end and reverse!
                            if (!newline_before) {
                                newline_before = buffer - 1;
                            }
                            //printf("file %s nl_before: %.20s\n", filename, (char*)newline_before + 1);
                            int rest = n - idx_of_string;
                            void *newline_after = memchr(pos, '\n', rest);
                            if (!newline_after) {
                                newline_after = buffer + n;  // CHANGE n was read_size be careful: read reads bufsize-1 bytes
                            }
                            //printf("file %s nl_after: %.20s\n", filename, (char*)newline_after);

                            int line_size = (char *)newline_after - (char *)newline_before;
                            char * sptr = (char *)newline_before + 1 ;
                            int maxlen = line_size - 1;
                            if(maxlen>80)
                                maxlen=80;
                            { std::lock_guard<std::mutex> lock(coutmtx);
                            printf("%s: %.*s\n", filename, maxlen, sptr );
                            }
/*
                            char * output = (char*)malloc(line_size); //includes +1 for \0 termination
                            char * pointer = (char*)newline_before + 1; // start at char after newline
                            for(int i=0; i < line_size - 1; i++){
                                output[i] = *(pointer+i);
                            }
                            output[line_size-1]='\0';
                            printf("%s: %s\n", filename, output);
                            free(output);
*/
                            ptr_to_next_pos = pos + searching_for.length();
                            if (ptr_to_next_pos < buffer + n) {
                                bckp_buffer = ptr_to_next_pos;
                            } else {
                                break;
                            }

                        } // enf of while
                        #endif
                    }
                if (n < read_size)
                    break; // saves one read call
                } // end while
            } // end if fd>0
            close(fd);
            
        // KNOWN GOOD VERSION:
        } else if (search_mode == "grepCPP") { // using C++ std
            std::ifstream src(filename);
            if (!src.good()) {
                std::cout << "error with filename: " << filename << std::endl;
                //throw std::ios::failure("src is no good: " ); don't throw or else atomic won't be decremented
            }
            unsigned long long linenr = 0;

            while (src.good()) {
                std::string line;
                std::getline(src, line);
                linenr++;
                std::string::size_type n;

                if ((n = line.find(searching_for)) != std::string::npos) {
                    std::lock_guard<std::mutex> lock(coutmtx);
                    //std::cout << "linenr: " << linenr << " pos: " << n << " in file: " << filename
                    //          << " " << line.substr(0, 79) << std::endl;
                    std::cout << filename << ": " << line.substr(0, 79) << std::endl;
                    hits++;
                    hash = 1;
                }
            }
            src.close();

        } else if (search_mode == "grepCCI") { // no binary
            std::ifstream src(filename);
            if (!src.good()) {
                std::cout << "error with filename: " << filename << std::endl;
                //throw std::ios::failure("src is no good: " ); don't throw or else atomic won't be decremented
            }
            unsigned long long linenr = 0;

            while (src.good()) {
                std::string line;
                std::getline(src, line);
                linenr++;
                std::string::size_type n;

                if ((n = line.find('\0')) != std::string::npos) {
                    //std::lock_guard<std::mutex> lock(coutmtx);
                    //std::cout << filename << " is binary" << std::endl;
                    src.close();
                    hash = -1;
                    break;
                }

                #ifdef USEAVX2
                if ( (n=avx2_strstr_v2(line, searching_for)) != std::string::npos ) {
                #endif
                #ifndef USEAVX2
                if ((n = line.find(searching_for)) != std::string::npos) {
                #endif
                //if ((n = line.find(searching_for)) != std::string::npos) {
                //if ( (n=avx2_strstr_v2(line, searching_for) != std::string::npos ){
                    std::lock_guard<std::mutex> lock(coutmtx);
                    //std::cout << "linenr: " << linenr << " pos: " << n << " in file: " << filename
                    //          << " " << line.substr(0, 79) << std::endl;
                    std::cout << filename << ": " << line.substr(0, 79) << std::endl;
                    hits++;
                    hash = 1;
                }
            }
            src.close();

        } else {  // find-mode
            std::string s = filename;
            if (s.find(searching_for) != std::string::npos) {
                std::lock_guard<std::mutex> lock(coutmtx);
                std::cout << "" << filename << std::endl;
                hash = hits++;
            }
        }

        return hash;
    }

private:
const int worker_id = -272727;
std::string start_with_path;
unsigned int buffer_size = 1024*1024 + 1; // smaller menas earlier exit on binary files
unsigned int read_size = buffer_size - 1;
char *buffer;  // move malloc in Worker class, to alloc once.

int do_dir_walking(const std::string &sInputPath) {
#ifdef DEBUG
    {
        std::lock_guard<std::mutex> lock(coutmtx);
        std::cout << worker_id << " do_dir_walking got: " << sInputPath << std::endl;
    }
#endif
    int error_code = 1;

    // processing each file in directory
    DIR *dir_handle;
    struct dirent *dir;
    dir_handle = opendir(sInputPath.c_str());
    if (dir_handle) {
        error_code = 0;
        while ((dir = readdir(dir_handle)) != NULL) {
            if (dir->d_type == DT_REG) {
                std::string sFileName = sInputPath + dir->d_name;
                auto inode = dir->d_ino;
#ifdef DEBUG
                { std::lock_guard<std::mutex> lock(coutmtx);
                    std::cout << "inode for file: " << sFileName << " is: " << inode << std::endl; }
#endif
                int hash = find_or_grep(sFileName.c_str());
                if (hash != -1) {
                    tls_filenames.emplace_back(std::to_string(inode) + ";" + sFileName + ";" + std::to_string(hash));
                }

            } else if (dir->d_type == DT_DIR) {
                std::string sname = dir->d_name;
                if (sname != "." && sname != "..") {
                    tls_directories.emplace_back(sInputPath + sname + "/");
                }
            }
        }
        closedir(dir_handle);
        return error_code;
    } else {
        std::lock_guard<std::mutex> lock(coutmtx);
        std::cerr << "Cannot open input directory: " << sInputPath << std::endl;
        return error_code;
    }
}
}; // end of class Worker


void do_startup_file_walking(std::string starting_path) {
    try {
        Worker w(-42,"empty");
        std::filesystem::path path(starting_path);
        std::filesystem::directory_iterator dir_iter(path, std::filesystem::directory_options::skip_permission_denied);
        std::filesystem::directory_iterator end;
        while (dir_iter != end) {
            std::filesystem::path path = dir_iter->path();
            if ((std::filesystem::is_directory(path) || std::filesystem::is_regular_file(path)) && !std::filesystem::is_symlink(path)) {
                std::string path_entry = std::filesystem::canonical(path);
                if (std::filesystem::is_directory(path)) {
#ifdef DEBUG
                    std::cout << "S directory: ";
                    std::cout << path_entry << " | " << path << std::endl;
#endif
                    if (path_entry != "/proc" && path_entry != "/sys") {
                        global_directories.emplace_back(path_entry + "/");  // add trailing /
                    }
                } else if (std::filesystem::is_regular_file(path)) {
#ifdef DEBUG
                    std::cout << "S file: ";
                    std::cout << path_entry << std::endl;
#endif
                    //Worker * w = new Worker(-42,"empty");
                    
                    int hash=w.find_or_grep(path_entry.c_str());
                    if(hash != -1){
                        struct stat s_stat;
                        if (lstat(path_entry.c_str(), &s_stat) == 0){
                            auto inode = s_stat.st_ino;
                            global_filenames.emplace_back(std::to_string(inode) + ";" + path_entry + ";" + std::to_string(hash) );
                        }
                    }
                }
            }
            ++dir_iter;
        }
    } catch (const std::exception &e) {
        std::cerr << "exception: do_startup_file_walking(): " << e.what() << '\n';
    }
}

int main(int argc, char* argv[]) {
    
    int n_threads = -1;
    n_threads = std::thread::hardware_concurrency();
    if (argc > 1) {
                try {
                n_threads=std::stoi(argv[1]);}
                catch (const std::exception &e){
                    std::cerr << "argv1 is no integer in function: " << e.what() << std::endl;
                    return -1;
                }
    }
    
    std::string argumentv_search_string;
    if(argc >= 4){
        argumentv_search_string = std::string(argv[2]);
    } else {
        std::cout << "usage: " << argv[0] << " <number of threads> <path> <search_string> [grep|grep-binary|find]" << "\ndefault:find" << std::endl; 
        return -1;
    }
    searching_for = argv[3];
    
    if(argc >= 5){
        if(std::string{argv[4]} != "" ){
            search_mode = std::string{argv[4]};
        }
    }
    std::cout << "Searching for: " << searching_for << " in mode " << search_mode  ; // more like find -type f <path> | grep string
    if(search_mode == "find")
        std::cout << " NOTE: also searching in file_path part not just after last /";
    std::cout << std::endl;

    std::vector<std::thread> threads;
    std::vector<Worker> workers;
    global_exceptions.clear();

    if(argumentv_search_string != ""){
        do_startup_file_walking(argumentv_search_string);
    } else {
        do_startup_file_walking("./");
    }

    int starting_dirs=global_directories.size();
    
    #ifndef PRINTFILENAMES
    std::cout << "starting_dirs: " << starting_dirs << " nthreads " << n_threads  << std::endl;
    #endif

    #ifdef DEBUG
    std::cout << "global_dirs size: " << starting_dirs << std::endl;
    #endif    

    #ifdef DEBUG
    if(starting_dirs < n_threads){
        std::cout << "less dirs than threads to begin with" << std::endl;
    }
    else {
        std::cout << "more or equal dirs as threads to begin with" << std::endl;
    }
    #endif

    for(int i=0; i < n_threads; ++i){
        if(starting_dirs < n_threads){
            if(i < starting_dirs){
                workers.push_back(Worker(i, global_directories.back())); // same
                global_directories.pop_back();                           // same
            } else {
                workers.push_back( Worker(i, "") );
            }
        } else {
            workers.push_back(Worker(i, global_directories.back()));     // same
            global_directories.pop_back();                               // same
        }   
    }

    // fill queues
    int tmp=0;
    while(!global_directories.empty()) {
        directories_array_of_vectors[(tmp++%n_threads)%NR_GLOBAL_DIRECTORIES_MUTEXES].push_back(global_directories.back());
        global_directories.pop_back();
    }

    #ifdef DEBUG
    for(int i=0; i<NR_GLOBAL_DIRECTORIES_MUTEXES; i++){
        std::cout << "directories_array_of_vectors[" << i << "].size(): " << directories_array_of_vectors[i].size() << std::endl;
        for(auto const &v: directories_array_of_vectors[i]){
            std::cout << "directories_array_of_vectors[" << i << "] = " << v << std::endl;
        }
    }
    std::cout << "starting glob_dirs: residual-size: " << global_directories.size()<< std::endl;
    for(auto &v: global_directories){
        std::cout << "starting glob_dirs: " << v << std::endl;
    }
    #endif    

    // start threads(Worker)
    for(int i=0; i < n_threads; ++i){
        threads.emplace_back( std::ref(workers[i]) );
    }

    #ifdef DEBUG     
    std::cout << "in main: workers size: " << workers.size() << std::endl;
    std::cout << "in main: threads size: " << threads.size() << std::endl;
    #endif   

    for(auto &v: threads){
        v.join();
    }

    /* process exceptions from threads */
    for(auto const &e: global_exceptions){
        try{
            if(e != nullptr){
                std::rethrow_exception(e);
            }
        } 
        catch(std::exception const &ex) {
                std::cerr << "EZZOZ exception: " << ex.what() << std::endl;
        }
    }

    if(global_directories.empty()){
        #ifdef DEBUG        
        std::cout << "global_directories is empty" << std::endl;
        #endif
    }
    else {
        std::cout << "global_directories HAS MORE WORK => needs next round of threads!" << std::endl;
        for(auto const& v: global_directories){
            std::cout << "globdirs-residuals: " << v << std::endl;
        }
    }   

    for(int i=0; i<NR_GLOBAL_DIRECTORIES_MUTEXES; i++){
        if(!directories_array_of_vectors[i].empty()){
            std::cout << "MORE WORK: directories_array_of_vectors[" << i << "].size(): " << directories_array_of_vectors[i].size() << std::endl;
            for(auto const &v: directories_array_of_vectors[i]){
                std::cout << "directories_array_of_vectors[" << i << "] = " << v << std::endl;
            }
        }
    }

    #ifdef DEBUG
    std::cout << "Global Filenames: (size: " << global_filenames.size() << ")" << std::endl;
    #endif    

    int file_sum=global_filenames.size();
    #ifdef PRINTFILENAMES    
    for(auto const &v: global_filenames){
        std::cout << v << std::endl;
    } 
    #endif    
    
    std::cout << "SEARCH HIT COUNT: " << hits << std::endl;
    
    std::ofstream dest("output", std::ios::binary);
    if(!dest)
        throw std::ios::failure(__FILE__ ":" + std::to_string(__LINE__));
    //dest << "inode;file;hash" << std::endl; // wirte csv header

    for(int i=0; i<NR_GLOBAL_FILENAME_MUTEXES; i++){
        #ifndef PRINTFILENAMES        
        std::cout << "filename_array_of_vectors[" << i << "].size(): " << filename_array_of_vectors[i].size() << std::endl;
        #endif        
        if(filename_array_of_vectors[i].size() > 0){
            file_sum+=filename_array_of_vectors[i].size();
        }
   
        for(auto const& v: filename_array_of_vectors[i]){
            #ifdef PRINTFILENAMES            
            std::cout << v << "\n";
            #endif
            dest << v << "\n"; // WRITE TO dest == ./output file
        }
    }
    
    for(auto const &v: global_filenames){
        dest << v << "\n";
    } 
    
    dest.close();
    
    #ifndef PRINTFILENAMES    
    std::cout << "filename_array_of_vectors.size(): " << filename_array_of_vectors.size() << std::endl;
    std::cout << "number of files: file_sum: " << file_sum << std::endl;
    #endif

    std::cout << "file size SUM: " << sum_file_size << std::endl;

    //std::sort(running_times.begin(), running_times.end());
    //for(auto v: running_times){
    //    std::cout << v << std::endl;
    //}
    //std::cout << "MAX_MIN:" << std::endl;
    //auto minmax = std::minmax_element(running_times.begin(), running_times.end());
    //// std::make_pair(first, first)
    //if(minmax != std::make_pair(running_times.begin(),running_times.begin() ) ){
    //    std::cout << "max: " << *minmax.second << " min: " << *minmax.first << std::endl;
    //}
    //auto dif = *minmax.second - *minmax.first;
    //std::cout << "difference(microsecs) between max and min: " << dif << " millisecs: " << dif/1000 << std::endl;

} // end int main(int argc, char* argv[])



