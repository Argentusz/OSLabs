#include <iostream>
#include <stdlib.h>
#include <aio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <chrono>
#include <thread>
#include <cstring>

//#define OPERATION_CNT 16
//#define BUFFER_SIZE 65536

struct aio_operation {
    struct aiocb aio;
    int write_operation;
    void* next_operation;
    bool done;
    int op_cnt;
    int buf_size;
};

bool anyDone = false;

void onCompletion(sigval_t);
char const * toExtensionPtr(char const *);
char * makeCopyName(char const * );

int main(int argc, char ** argv) {
    int const OPERATION_CNT = argc > 1 ? static_cast<int>(strtol(argv[1], nullptr, 10)) : 4;
    int const BUFFER_SIZE = argc > 2 ? static_cast<int>(strtol(argv[2], nullptr, 10)) : 65536;
    char const * srcName = argc > 3 ? argv[3] : "Tenet.mp4";
    char const * dstName = argc > 3 ? makeCopyName(srcName) : "teneT.mp4";
    char** buffer;
    int src;
    int dst;
    long srcSize;
    unsigned long clusterSize;
    struct stat fileStatistics{};
    struct statvfs clusterBuffer{};
    struct aiocb * allOperations[OPERATION_CNT * 2];
    struct aio_operation readers[OPERATION_CNT];
    struct aio_operation writers[OPERATION_CNT];
    int doneCnt;
    struct aio_operation placeholder{};
    int exitCode = EXIT_SUCCESS;
    std::chrono::steady_clock::time_point begin;
    std::chrono::steady_clock::time_point end;
    if (argc < 5) std::cout << std::endl;

    // Opening files
    src = open(srcName, O_RDONLY | O_NONBLOCK, 0666);
    if (src == -1) {
        std::cerr << "Could not open " << srcName << std::endl;
        exitCode = EXIT_FAILURE;
        goto close_src;
    }
    dst = open(dstName, O_CREAT | O_WRONLY | O_TRUNC | O_NONBLOCK, 0666);
    if (dst == -1) {
        std::cerr << "Could not open " << dstName << std::endl;
        exitCode = EXIT_FAILURE;
        goto close_dst;
    }

    // Get file statistics
    if (fstat(src, &fileStatistics) == -1) {
        std::cerr << "fstat err" << std::endl;
        exitCode = EXIT_FAILURE;
        goto close_dst;
    }
    srcSize = fileStatistics.st_size;
    if (argc < 5) std::cout << "File size: " << srcSize << " bytes" << std::endl;

    // Get cluster size
    if (statvfs("/", &clusterBuffer) == -1) {
        std::cerr << "statvfs err" << std::endl;
        exitCode = EXIT_FAILURE;
        goto close_dst;
    }
    clusterSize = clusterBuffer.f_bsize;

    if (argc < 5) std::cout << "Operations amount: " << OPERATION_CNT << std::endl;
//    std::cout << "Cluster size: " << clusterSize << " bytes" << std::endl;
    if (argc < 5) std::cout << "Buffer size: " << BUFFER_SIZE << " bytes" << std::endl;

    buffer = (char**)malloc(OPERATION_CNT * sizeof(char*));
    for (char ** ptr = buffer; ptr - buffer < OPERATION_CNT; ptr++) {
        *ptr = (char*)malloc(BUFFER_SIZE);
        for (char * cptr = *ptr; cptr - *ptr < BUFFER_SIZE; cptr++) {
            *cptr = 0;
        }
    }

    for (int i = 0; i < OPERATION_CNT; i++) {
        // Operations arrays
        aio_operation reader{};
        aio_operation writer{};
        // Files
        reader.aio.aio_fildes = src;
        writer.aio.aio_fildes = dst;
        // Buffers
        reader.aio.aio_buf = buffer[i];
        writer.aio.aio_buf = buffer[i];
        reader.aio.aio_nbytes = BUFFER_SIZE;
        writer.aio.aio_nbytes = BUFFER_SIZE;
        // Determine if it is reader or writer
        reader.write_operation = 0;
        writer.write_operation = 1;
        // Position in file
        reader.aio.aio_offset = BUFFER_SIZE * i;
        writer.aio.aio_offset = BUFFER_SIZE * i;
        // Notification method
        reader.aio.aio_sigevent.sigev_notify = SIGEV_THREAD;
        writer.aio.aio_sigevent.sigev_notify = SIGEV_THREAD;
        // Operation completion handler
        reader.aio.aio_sigevent.sigev_notify_function = onCompletion;
        writer.aio.aio_sigevent.sigev_notify_function = onCompletion;
        reader.aio.aio_sigevent.sigev_notify_attributes = nullptr;
        writer.aio.aio_sigevent.sigev_notify_attributes = nullptr;

        reader.done = false;
        writer.done = false;
        placeholder.done = false;

        reader.buf_size = BUFFER_SIZE;
        writer.buf_size = BUFFER_SIZE;
        reader.op_cnt = OPERATION_CNT;
        writer.op_cnt = OPERATION_CNT;

        // Pushing to corresponding array
        readers[i] = reader;
        writers[i] = writer;

        // sig destination
        readers[i].aio.aio_sigevent.sigev_value.sival_ptr = &readers[i];
        writers[i].aio.aio_sigevent.sigev_value.sival_ptr = &writers[i];
        // Linking corresponding readers and writers
        readers[i].next_operation = &writers[i];
        writers[i].next_operation = &readers[i];
    }

    for (int i = 0; i < OPERATION_CNT; i++) {
        allOperations[i * 2] = &readers[i].aio;
        allOperations[i * 2 + 1] = &writers[i].aio;
    }

    begin = std::chrono::steady_clock::now();

    for (auto & reader : readers) {
        if (aio_read(&reader.aio) == -1) {
            std::cerr << "Error while reading" << std::endl;
            exitCode = EXIT_FAILURE;
            goto free_mem;
        }
    }


    while(doneCnt < OPERATION_CNT) {
        aio_suspend(allOperations, OPERATION_CNT * 2, nullptr);
        if (anyDone) {
            for (int i = 0; i < OPERATION_CNT; i++) {
                if (readers[i].done && writers[i].done) {
                    allOperations[i * 2] = &placeholder.aio;
                    allOperations[i * 2 + 1] = &placeholder.aio;
                    doneCnt++;
                }
            }
        } else {
//            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            sleep(1);
        }
    }

    end = std::chrono::steady_clock::now();
//    std::cout << "File " << srcName << " copied into " << dstName << std::endl;
    std::cout << "Time elapsed: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "ms" << std::endl;
    // Deferred instructions
    free_mem:
        for (int i = 0; i < OPERATION_CNT; i++)
            free(buffer[i]);
        free(buffer);
    close_dst:
        close(dst);
    close_src:
        close(src);

    return exitCode;
}

void onCompletion(sigval_t sigval) {
    auto * done = (struct aio_operation *)sigval.sival_ptr;
    auto * next = (struct aio_operation *)done->next_operation;
    (&done->aio)->aio_offset += done->buf_size * done->op_cnt;
    if (done->write_operation) {
        aio_read(&next->aio);
    } else {
        auto ret = aio_return(&done->aio);
        if (ret) {
            (&next->aio)->aio_nbytes = ret;
            aio_write(&next->aio);
        } else {
            done->done = true;
            next->done = true;
            anyDone = true;
        }
    }
}

char const * toExtensionPtr(char const * str) {
    char const * ptr;
    for (ptr = str; *ptr != '.' && *ptr != '\0'; ptr++);
    if (*ptr == '\0') return nullptr;
    return ptr;
}

char * makeCopyName(char const * srcName) {
    char const * extPtr = toExtensionPtr(srcName);
    char * res;
    if (extPtr == nullptr) {
        res = static_cast<char*>(malloc(5));
        res[0] = 'c';
        res[1] = 'o';
        res[2] = 'p';
        res[3] = 'y';
        res[4] = '\0';
        return res;
    }
    res = static_cast<char *>(malloc((strlen(srcName) + 6)));

    char const * srcPtr = srcName;
    char * resPtr = res;
    while(srcPtr != extPtr) {
        *resPtr = *srcPtr;
        resPtr++;
        srcPtr++;
    }
    *resPtr = 'c'; resPtr++;
    *resPtr = 'o'; resPtr++;
    *resPtr = 'p'; resPtr++;
    *resPtr = 'y'; resPtr++;
    while(*srcPtr != '\0') {
        *resPtr = *srcPtr;
        resPtr++;
        srcPtr++;
    }
    *resPtr = *srcPtr;
    return res;
}
