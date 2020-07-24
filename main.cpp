/*

notes:

    - g++ -O3 -std=c++17 -pthread main.cpp -o main && time sudo ./main 8 / # 1.3 sec
    use -mno-omit-leaf-frame-pointer -fno-omit-frame-pointer for stack traces
time ./main 8 /home/bm # 0.4sec
threads are not synchonized or based on condition variable if work==more directories are available,
just stealing from the global std::string vectors holding the directory names.
Some threads might die early, and the last thread might scan the residual dirs.

    - Dcache scalability and RCU-walk: https://lwn.net/Articles/419811/

bpftrace -b -e 'tracepoint:sched:sched_process_exec {printf("%d %d %s %d\n", pid, uid, comm, (*curtask->mm->exe_file->f_inode).i_ino)} '

curtask->mm->exe_file->f_inode->i_ino

# for more open files
ulimit -n 123456
# for more threads > 32k; I guess 2 mmap's per thread
echo 777888 >  /proc/sys/vm/max_map_count

e.g. use `iostat -x -p nvme0n1p1 -h 1` to find sweet spot
mpstat -P ALL 1
vmstat 1
vmstat -m 1  | egrep "dentry|inode_cache" # print slabs

read-ahead settings, scheduler, etc: https://cromwell-intl.com/open-source/performance-tuning/disks.html
https://wiki.mikejung.biz/Ubuntu_Performance_Tuning

echo kyber > /sys/block/sda/queue/scheduler
echo 512 > /sys/block/sda/queue/nr_requests
echo 0 > /sys/block/sda/queue/read_ahead_kb

test filesystem on samsung nvme:
128.000 directories
1.350.000 files
209GB
time: 5 sec with 64-128 threads
*/


#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>


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

//#define DEBUG 0
 
thread_local std::string tls_path;
thread_local std::vector<std::string> tls_filenames;
thread_local std::vector<std::string> tls_directories;

//struct container{std::mutex mtx; std::vector<std::string> vec;};
//std::array<container, 8> files_container;

// note: eliminate false sharing
#define NR_GLOBAL_FILENAME_MUTEXES 8
std::array<std::mutex, NR_GLOBAL_FILENAME_MUTEXES> global_filename_mutexes;
std::array<std::vector<std::string>, NR_GLOBAL_FILENAME_MUTEXES> filename_array_of_vectors;

#define NR_GLOBAL_DIRECTORIES_MUTEXES 16
std::array<std::mutex, NR_GLOBAL_DIRECTORIES_MUTEXES> global_directories_mutexes;
std::array<std::vector<std::string>, NR_GLOBAL_DIRECTORIES_MUTEXES> directories_array_of_vectors;

std::vector<std::string> global_filenames(0);
std::vector<std::string> global_directories(0);
std::vector<std::exception_ptr> global_exceptions;
std::mutex coutmtx, global_exceptmutex;

int global_residual_dirs=-1;
int starting_dirs=-1;

class Worker {
public:
    Worker(int n, std::string s): worker_id(n), start_with_path(s) {}
    void operator()(){
        try{
            if(start_with_path != ""){
                tls_path=start_with_path;
                do_linear_descent();
                working();
            }
            else{
                #ifdef DEBUG                
                {std::lock_guard<std::mutex> lock(coutmtx);
                 std::cout << worker_id << ": EMPTY constructor string." << std::endl;}
                #endif            

                for(int i=0; i< 13; i++){    // try _ times to get some work
                    #ifndef DONTSLEEP                
                    std::this_thread::sleep_for(std::chrono::milliseconds(2 + worker_id%15)); 
                    // helps to balance work for initially unemployed threads
                    // check the filename_array_of_vectors[i].size() output at the end for even distribution
                    #endif                    
                    working(); 
                }
            }

        } catch(...){
            std::lock_guard<std::mutex> lock(global_exceptmutex);
            global_exceptions.push_back(std::current_exception());
        }
    }

    void working(){
        #ifdef DEBUG
        {std::lock_guard<std::mutex> lock(coutmtx);
        std::cout << worker_id << ": constructor string: " << tls_path << std::endl;}
        #endif

    label_new:
        int proceed_new=0;
        #define NR_OF_ROUNDS 2// a few or just one round works qually well
        for(int i=0; proceed_new == 0 && i < NR_OF_ROUNDS*NR_GLOBAL_DIRECTORIES_MUTEXES; i++){ // 
            int start_with_i=(worker_id+i)%NR_GLOBAL_DIRECTORIES_MUTEXES; // start with own id-vector
            std::lock_guard<std::mutex> lock(global_directories_mutexes[start_with_i]);
            if(!directories_array_of_vectors[start_with_i].empty()){
                tls_path=directories_array_of_vectors[start_with_i].back();
                directories_array_of_vectors[start_with_i].pop_back();
                proceed_new=1;
            }
        }
        if(proceed_new == 1){ // equal to if(tls_path != "") 
            do_linear_descent();
        }
        else{
            #ifdef NEWDEBUG
            {std::lock_guard<std::mutex> lock(coutmtx);
             std::cout << worker_id << " is dying: did not find work in directories_array_of_vectors" << std::endl;
            }
            #endif
            goto end_label_new; // thread will die here
        }
        goto label_new; // check if there is more work

    end_label_new:  return;

    }

/*
 descent into ONE subdirectory found by `do_tree_walking()` linearly,
 meaning: step into ONE directory after another, till the end of the directory tree
*/

    void do_linear_descent() {
        do {
            #ifdef DEBUG
            if(tls_path == ""){
                    std::lock_guard<std::mutex> lock(coutmtx);
                    std::cout << worker_id << ": ERROR tls_path " << tls_path << std::endl;
            }
            #endif                    
            do_dir_walking(tls_path);
            //tls_path=""; // don't walk into same dir again
           
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
            // save filenames to global vector specific to this thread
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
    }


int elf_hash(const char *filename){
    int hash=-1;
    
    int fd = open(filename, O_RDONLY);
    if(fd>0){
        //printf("open of %s succeded\n", filename);
        #define BUFSIZE 128
        #define ELFMAGICSIZE 4
        char buf[BUFSIZE];
        int is_elf=0;
        int n_elf=read(fd, &buf, ELFMAGICSIZE);
        if(n_elf == ELFMAGICSIZE){
            if(buf[0] =='\x7f' && buf[1] == 'E' && buf[2]=='L' && buf[3]=='F'){
                is_elf=1;
            }
        }
        else {
            //std::cout << filename << " file too small" << std::endl;
            close(fd);
            return -1;
        }
        int  n_read=-1;
        if (is_elf == 1){
            n_read=read(fd, &buf[4], BUFSIZE-ELFMAGICSIZE);
        }
        
        close(fd);
    
        //printf(" open of %s succeded and did read : %d bytes\n", filename, n_read);
        
        if(n_read == (BUFSIZE - ELFMAGICSIZE)){
            hash=5381;
            for(int i=4; i<BUFSIZE; i++){
                hash = ((hash << 5) + hash) + buf[i];
            }   
                //std::cout << "hash for: " <<filename << " is: "  << hash << std::endl;
        } else if(is_elf == 1){
            #ifndef PRINTFILENAMES
            printf("%s: elf but smaller than: %d\n", filename, BUFSIZE);
            #endif
        }
    }
    else {
        close(fd);
    }
    return hash;
}




int elf_hash_128(const char *filename){
    int hash=-1;
    //int closed=0;
    int fd = open(filename, O_RDONLY);
    if(fd>0){
        //printf("open of %s succeded\n", filename);
        #define BUFSIZE 128
        char buf[BUFSIZE];
        int n_read=read(fd, &buf, BUFSIZE);
        //int n_read=0;
        close(fd);
        //closed=1;
        //printf(" open of %s succeded and did read: %d\n", filename, n_read);
        int is_elf=0;
        if(n_read>=4){
            if(buf[0] =='\x7f' && buf[1] == 'E' && buf[2]=='L' && buf[3]=='F'){
                is_elf=1;
                if(n_read == BUFSIZE){
                    hash =5381;
                    for(int i=4; i<BUFSIZE; i++){
                        hash = ((hash << 5) + hash) + buf[i];
                    }   
                //std::cout << "hash for: " <<filename << " is: "  << hash << std::endl;
                
                } else if(is_elf == 1){
                    #ifndef PRINTFILENAMES
                    printf("%s: elf but smaller than: %d\n", filename, BUFSIZE);
                    #endif
                }
            }
        }
    }
     else {close(fd);}
    //if(!closed)
    //    close(fd);
    return hash;
}


private:
    const int worker_id = -272727;
    std::string start_with_path;


int do_dir_walking(const std::string &sInputPath) {
    #ifdef DEBUG
    {
        std::lock_guard<std::mutex> lock(coutmtx);
        std::cout << worker_id << " do_dir_walking got: " << sInputPath << std::endl;
    }
    #endif    
    int error_code = 1;
    struct stat s_stat;

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
                {std::lock_guard<std::mutex> lock(coutmtx);
                std::cout << "inode for file: " << sFileName << " is: " << inode << std::endl;}
                #endif

                #ifdef WITH_LSTAT   
                if (lstat(sFileName.c_str(), &s_stat) == 0){
                    if(s_stat.st_size >= 128){
                        
                        int hash=elf_hash_128(sFileName.c_str()); // read 128 for every file with st_size >= 128 bytes
                        if(hash != -1){
                            tls_filenames.push_back(std::to_string(inode) + ";" + sFileName + ";" + std::to_string(hash));
                        }
                    }
                }
                
                #endif
                #ifndef WITH_LSTAT
                int hash=elf_hash(sFileName.c_str()); // read 4 than 128 bytes if it's an ELF
                if(hash != -1){
                    tls_filenames.emplace_back(std::to_string(inode) + ";" + sFileName + ";" + std::to_string(hash) );
                }
                #endif
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

};


void do_startup_file_walking(std::string starting_path){
    try {
        std::filesystem::path path(starting_path);
        std::filesystem::directory_iterator dir_iter(path, std::filesystem::directory_options::skip_permission_denied);
        std::filesystem::directory_iterator end;
        while (dir_iter != end) {
            std::filesystem::path path = dir_iter->path();
            if((std::filesystem::is_directory(path) || std::filesystem::is_regular_file(path)) && !std::filesystem::is_symlink(path)){
                std::string path_entry = std::filesystem::canonical(path);
                if (std::filesystem::is_directory(path)) {
                    #ifdef DEBUG
                    std::cout << "S directory: ";
                    std::cout << path_entry << " | " << path << std::endl;
                    #endif
                    if(path_entry != "/proc" && path_entry != "/sys" ){
                        global_directories.emplace_back(path_entry + "/"); // add trailing /
                    }
                } else if (std::filesystem::is_regular_file(path)) {
                    #ifdef DEBUG                    
                    std::cout << "S file: ";
                    std::cout << path_entry << std::endl;
                    #endif                    
           

                    //global_filenames.emplace_back(path_entry);
                    Worker w(-42,"empty");
                    int hash=w.elf_hash(path_entry.c_str()); 
                    
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
    } catch (const std::exception& e) {
        std::cerr << "exception: do_startup_file_walking(): " << e.what() << '\n';
    }
}

int main(int argc, char* argv[]) {
    #ifdef WITH_LSTAT   
    std::cout << "Running with lstat(2) call" << std::endl;
    #endif
    #ifdef DEBUG
    std::cout << "Running with DDEBUG" << std::endl;
    #endif
    #ifdef PRINTFILENAMES
    std::cout << "Running with DPRINTFILENAMES" << std::endl;
    #endif
    
    int n_threads = -1;
    n_threads = std::thread::hardware_concurrency();
    if (argc > 1) {
        #ifdef DEBUG        
        std::cout << "Using " << argv[1] << " threads\n" << std::endl;
        #endif        
        n_threads=std::stoi(argv[1]);
    } else {
        #ifdef DEBUG        
        std::cout << "Using " << n_threads << " threads\n" << std::endl;
        #endif        
    }

    std::string argumentv_string;
    if(argc == 3){
        argumentv_string = std::string(argv[2]);
    } else { std::cout << "usage: " << argv[0] << " <number of threads> <path>" << std::endl; }

    std::vector<std::thread> threads;
    std::vector<Worker> workers;
    global_exceptions.clear();

    if(argumentv_string != ""){
        do_startup_file_walking(argumentv_string);
    } else {
        do_startup_file_walking("./");
    }

    starting_dirs=global_directories.size();
    
    global_residual_dirs=n_threads-starting_dirs;
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

    // re-arrange loop ?
    for(int i=0; i < n_threads; ++i){
        if(starting_dirs < n_threads){
            if(i < starting_dirs){
                workers.push_back(Worker(i, global_directories.back())); // same
                global_directories.pop_back();                           // same
            } else {
                workers.push_back(Worker(i, ""));
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

    // dont forget global_filenames:
    #ifdef DEBUG
    std::cout << "Global Filenames: (size: " << global_filenames.size() << ")" << std::endl;
    #endif    

    int file_sum=global_filenames.size();
    #ifdef PRINTFILENAMES    
    for(auto const &v: global_filenames){
        std::cout << v << std::endl;
    } 
    #endif    
    
    std::ofstream dest("output", std::ios::binary);
    if(!dest)
        throw std::ios::failure(__FILE__ ":" + std::to_string(__LINE__));
    dest << "inode;file;hash" << std::endl; // wirte csv header

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
    // TODOOOOOOOOOO:  add hash and inodes for / files
    
    for(auto const &v: global_filenames){
        dest << v << "\n";
    } 
    
    dest.close();
    
    #ifndef PRINTFILENAMES    
    std::cout << "filename_array_of_vectors.size(): " << filename_array_of_vectors.size() << std::endl;
    std::cout << "number of files: file_sum: " << file_sum << std::endl;
    #endif

} // end int main(int argc, char* argv[])



