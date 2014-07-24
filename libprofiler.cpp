
#include <execinfo.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <stack>
#include <vector>
#include <string.h>
#include <string>
#include <pthread.h>
#include <sys/shm.h>
#include <cxxabi.h>
#include <ncurses.h>
#include <functional> 
#include <algorithm>
#include <time.h>
#include <inttypes.h>
#include <climits>
#include <math.h>
#include <set> // To be replaced by hashset ?

#define GET_EBX(var)  __asm__ __volatile__( "movl %%ebx, %0" : "=a"(var) ) 
#define SET_EBX(var)  __asm__ __volatile__( "movl %0, %%ebx" : :"a"(var) ) 

#define GET_RAX(var)  __asm__ __volatile__( "mov %%rax, %0" : "=a"(var) ) 
#define SET_RAX(var)  __asm__ __volatile__( "mov %0, %%rax" : :"a"(var) ) 

#define ORDER_COUNT    0
#define ORDER_CPU      1
#define ORDER_INNER    2
#define ORDER_AVG      3

#define MODE_LIST	   0
#define MODE_PATH	   1

#define SHARED_CHANNEL 24

int init=0;
int main_found=0;
void* _entryPointer=0;
int _orderMethod = 0;

typedef struct item {
	void *_func;
	void *_from;
	long _spent;
	timespec _time;
	long long _outerTime;
} ITEM;

struct item_method;

typedef struct item_caller {
	void *_callsite;
	item_method* _calls;
	char* _name;
	long _count;
	long long _cumulatedTime;
	long long _cumulatedOuterTime;
	void *_nextItem;

	long _min;
	long _max;
	long _distrib[32];

	int _touched;
	std::vector<item_caller*> _callslist;

	friend bool operator < (const item_caller & lhs, const item_caller & rhs)
	{
		if (_orderMethod==ORDER_COUNT) {
 			return (lhs._count < rhs._count ? true : false);
		} else if (_orderMethod==ORDER_CPU) {
 			return (lhs._cumulatedTime < rhs._cumulatedTime ? true : false);
		} else if (_orderMethod==ORDER_INNER) {
			return ((lhs._cumulatedTime-lhs._cumulatedOuterTime) < (rhs._cumulatedTime-rhs._cumulatedOuterTime) ? true : false);
		} else if (_orderMethod==ORDER_AVG) {
			return ((lhs._cumulatedTime/lhs._count) < (rhs._cumulatedTime/rhs._count) ? true : false);
		}
	}

	friend bool operator > (const item_caller & lhs, const item_caller & rhs)
	{
 		if (_orderMethod==ORDER_COUNT) {
 			return (lhs._count > rhs._count ? true : false);
		} else if (_orderMethod==ORDER_CPU) {
 			return (lhs._cumulatedTime > rhs._cumulatedTime ? true : false);
		} else if (_orderMethod==ORDER_INNER) {
			return ((lhs._cumulatedTime-lhs._cumulatedOuterTime) > (rhs._cumulatedTime-rhs._cumulatedOuterTime) ? true : false);
		} else if (_orderMethod==ORDER_AVG) {
			return ((lhs._cumulatedTime/lhs._count) > (rhs._cumulatedTime/rhs._count) ? true : false);
		}
	}

} ITEM_CALLER;

typedef struct item_method {
	void *_func;
	char* _name;
	long long _count;
	long long _cumulatedTime;
	long long _cumulatedOuterTime;
	long long _spent;
	void *_nextItem;

	ITEM_CALLER *_callers;
	ITEM_CALLER *_last_caller;
	long _numberCallers;

	friend bool operator < (const item_method & lhs, const item_method & rhs)
	{
		if (_orderMethod==ORDER_COUNT) {
 			return (lhs._count < rhs._count ? true : false);
		} else if (_orderMethod==ORDER_CPU) {
 			return (lhs._cumulatedTime < rhs._cumulatedTime ? true : false);
		} else if (_orderMethod==ORDER_INNER) {
			return ((lhs._cumulatedTime-lhs._cumulatedOuterTime) < (rhs._cumulatedTime-rhs._cumulatedOuterTime) ? true : false);
		} else if (_orderMethod==ORDER_AVG) {
			return ((lhs._cumulatedTime/lhs._count) < (rhs._cumulatedTime/rhs._count) ? true : false);
		}
	}
	friend bool operator > (const item_method & lhs, const item_method & rhs)
	{
 		if (_orderMethod==ORDER_COUNT) {
 			return (lhs._count > rhs._count ? true : false);
		} else if (_orderMethod==ORDER_CPU) {
 			return (lhs._cumulatedTime > rhs._cumulatedTime ? true : false);
		} else if (_orderMethod==ORDER_INNER) {
			return ((lhs._cumulatedTime-lhs._cumulatedOuterTime) > (rhs._cumulatedTime-rhs._cumulatedOuterTime) ? true : false);
		} else if (_orderMethod==ORDER_AVG) {
			return ((lhs._cumulatedTime/lhs._count) > (rhs._cumulatedTime/rhs._count) ? true : false);
		}
	}

} ITEM_METHOD;

template <class T>
bool CMP_LESS_ADAPT(T* i1, T* i2)
{
    return *i1 < *i2;
}

typedef struct path {

	long long _cpuTime;
	char* _primary_name;
	ITEM_CALLER* _method;
	std::vector<path*> _path;

	friend bool operator < (const path & lhs, const path & rhs)
	{
		return *lhs._method < *rhs._method;
	}

	friend bool operator > (const path & lhs, const path & rhs)
	{
 		return *lhs._method > *rhs._method;
	}

} PATH;

template <class T>
bool CMP_GREAT_ADAPT(T* i1, T* i2)
{
    return *i1 > *i2;
}

#define MAX_STACK_DEPTH 2048

typedef struct thread_context {
	long _threadid;
	ITEM* _stack;
	int _stackdepth;
	ITEM_METHOD* _first;   // Methods called by the thread
	ITEM_METHOD* _recorded;// Double linked list by thread  
	void *_nextItem;
//	void *_previousItem;
} THREAD_CONTEXT;

THREAD_CONTEXT* _firstThreadItem = 0;
THREAD_CONTEXT* _lastThreadItem = 0;
int _numberOfThreads = 0;

// ################################################################## //
// ## REDIRECTING STANDARD OUTPUT							  ## //
// ################################################################## //


#define COMMUNICATION_BUFFER_SIZE 256

char *shared_com; // Shared pointer
pthread_t output_thread;
pthread_t key_thread;
pthread_t distrib_thread;
pthread_t outbuilder_thread;
bool stop_output_thread = false;
bool stop_ncurses = false;
int pipe_fds[2];
int _display = 0;
WINDOW* aOutputWin;
WINDOW* aDisplayWin;
WINDOW* aGraphWin;

extern "C" int demangle(char* oBuffer, int iSize, void *func);

extern "C" void* redirected_thread(void* iArg);
extern "C" void* keyboard_thread(void* iArg);
extern "C" void* distribution_thread(void* iArg);

extern "C" void stopRedirectThread(){
	stop_output_thread = true;
}

extern "C" WINDOW *create_newwin(int height, int width, int starty, int startx)
{	WINDOW *local_win;

	local_win = newwin(height, width, starty, startx);
	
	wrefresh(local_win);		/* Show that box 		*/

	return local_win;
}

extern "C" void destroy_win(WINDOW *local_win)
{	
	wrefresh(local_win);
	delwin(local_win);
}


// We catch interruptions to stop properly the profiler
void intScreenBuilderHandler(int dummy) {
    // here we pass the info !! need to stop other process 
    printf("Screen builder received interrupt signal");	 
    shared_com[1] = 1;
    
    stop_ncurses = true;
    destroy_win(aOutputWin);
    destroy_win(aDisplayWin);
    destroy_win(aGraphWin);

    endwin();	

}	

pthread_mutex_t ncurses_mutex;

extern "C" void* screenOuptut(void* args){

	 bool stop = false;
	 char buf[16384];

	 while (!stop) {
	    
         int readen = read(0, &buf, 16384-1);
	    if (readen>0) {
		  buf[readen]=0;
		  if (stop_ncurses) printf("%s",buf);
		  
		  pthread_mutex_lock (&ncurses_mutex);
		  if ((!stop_ncurses) && ((_display % 3)==0))  wprintw(aOutputWin,"%s",buf);
		  if (!stop_ncurses) wrefresh(aOutputWin);
	    	  pthread_mutex_unlock (&ncurses_mutex);
	    
         } else {
		  usleep(10000);
	    }

 	    int status = shared_com[0];
	    if (status==2) { // Stop the process
		    stop = true;
	    }	 

	}

	return 0;

}

extern "C" bool needDisplay(int iPos, int& ioBuildPos, int oResizeScreen = 0){

	int aPos = iPos;
	int aOff = 0;
	
	if (shared_com[5]-(shared_com[7]-(1+oResizeScreen))>=0) {
		aOff = shared_com[5]-(shared_com[7]-(1+oResizeScreen))+1;
	} 
	aPos = iPos - aOff;

	if ((aPos>=0) && (aPos<shared_com[7]-(1+oResizeScreen))) {
		ioBuildPos = aPos+1;	
		return true;
	} else {
		ioBuildPos = aPos+1;	
		return false;
	}

}

extern "C" void screenBuilder(){

    bool stop = false;
    char buf[16384];
    shared_com[1] = 0;
    int prec_reverse = 0;

    signal(SIGINT, intScreenBuilderHandler);	

    // Init ncurses
    initscr();
    start_color();

    init_pair(1, COLOR_CYAN  , COLOR_BLACK);
    init_pair(2, COLOR_WHITE , COLOR_RED  );		

    curs_set(0); // remove cursor	

    int row, col;
    getmaxyx(stdscr,row,col);
    aOutputWin = create_newwin(row/2+1, col, row/2, 0);
    scrollok(aOutputWin, TRUE);

 	
    pthread_mutex_init (&ncurses_mutex, NULL);
    pthread_create(&outbuilder_thread, NULL, &screenOuptut, (void*) 0);	

    aDisplayWin = create_newwin(row/2, col, 0, 0);

    aGraphWin = create_newwin(row/2, col, row/2, 0);		

    // Here we start to read the ouput data 
    bool _have_to_wait = false;
    while (!stop) {
	    
	    pthread_mutex_lock (&ncurses_mutex);

	    shared_com[7] = row;			
	    int status = shared_com[0];
	    if (status==2) { // Stop the process
		    stop = true;
	    } else if (status==3) { // Write formatted data
		    // Here we have to write structured data to the screen	

		    if (shared_com[6]==MODE_LIST) {
			
			    // ################### LIST DISPLAY ######################## //	

			    if (shared_com[2]==1) {
			    		mvwprintw(aDisplayWin, 0,0," # Calls  | CPU Cycle | CPU Avg. | CPU Inn. | Caller | Calls | Function Name called by thread[%d]",shared_com[3]);
			    }

			    if (shared_com[4]==ORDER_COUNT) {
					 mvwchgat(aDisplayWin, 0, 0, 10, A_REVERSE, 1, 0);
			    } else if (shared_com[4]==ORDER_CPU) {
					 mvwchgat(aDisplayWin, 0, 11, 11, A_REVERSE, 1, 0);
			    } else if (shared_com[4]==ORDER_AVG) {
					 mvwchgat(aDisplayWin, 0, 23, 10, A_REVERSE, 1, 0);
			    } else if (shared_com[4]==ORDER_INNER) {
					 mvwchgat(aDisplayWin, 0, 34, 10, A_REVERSE, 1, 0);
			    }

			    if (!stop_ncurses) {
				   shared_com[col+SHARED_CHANNEL] = 0; // Set maximum length of STR
				   			
				   mvwprintw(aDisplayWin, shared_com[2],0,"%s",(char*) (shared_com+SHARED_CHANNEL));
				   
			    }
	
			    // Setup reverse line
		    	    mvwchgat(aDisplayWin, prec_reverse, 0, -1, A_NORMAL, 0, 0);
		    	    mvwchgat(aDisplayWin, shared_com[5]+1, 0, -1, A_REVERSE, 0, 0);
		    	    prec_reverse = shared_com[5]+1;		

			    shared_com[0] = 0; // Reset we can display the other

		    } else if (shared_com[6]==MODE_PATH) {
	
			    // ################### PATH DISPLAY ######################## //

			    if (shared_com[2]==1) {

					// %14p | %09lu | %8s | %8s | %8s |   %s%s\n	
			    		mvwprintw(aDisplayWin, 0,0," Address       | # Calls   | CPU Cyc. | CPU Avg. | CPU Inn. | Longest Path Thread [%d]",shared_com[3]);
			    }
	

			    if (shared_com[4]==ORDER_COUNT) {
					 mvwchgat(aDisplayWin, 0, 0 +16, 11, A_REVERSE, 1, 0);
			    } else if (shared_com[4]==ORDER_CPU) {
					 mvwchgat(aDisplayWin, 0, 11+17, 10, A_REVERSE, 1, 0);
			    } else if (shared_com[4]==ORDER_AVG) {
					 mvwchgat(aDisplayWin, 0, 23+16, 10, A_REVERSE, 1, 0);
			    } else if (shared_com[4]==ORDER_INNER) {
					 mvwchgat(aDisplayWin, 0, 34+16, 10, A_REVERSE, 1, 0);
			    }	

			    if (!stop_ncurses) {
				   shared_com[col+SHARED_CHANNEL] = 0; // Set maximum length of STR
				   		
				   int aPos = 0;
				   if (needDisplay(shared_com[2], aPos)) {
						
				   		mvwprintw(aDisplayWin, aPos,0,"%s",(char*) (shared_com+SHARED_CHANNEL));
						if (shared_com[9]==1) {
							mvwchgat(aDisplayWin, aPos, 61, -1, A_NORMAL, 2, 0);
						} else {
							mvwchgat(aDisplayWin, aPos, 61, -1, A_NORMAL, 0, 0);
						}

				   }
			    }

			    // Setup reverse line
			    int aREVERSE = 0;
			    needDisplay(shared_com[5]+1, aREVERSE, 1);
		    	    mvwchgat(aDisplayWin, prec_reverse, 0, -1, A_NORMAL, 0, 0);
		    	    mvwchgat(aDisplayWin, aREVERSE, 0, -1, A_REVERSE, 0, 0);
		    	    prec_reverse = aREVERSE;		

			    shared_com[0] = 0; // Reset we can display the other	

		    }


	    } else if (status==5) {
		    wclear(aDisplayWin);
		    shared_com[0] = 0; 
	    } else if (status==10) {
		    if (!stop_ncurses) wrefresh(aDisplayWin);
		    _have_to_wait = true;	
		    shared_com[0] = 0;
	    } else {
		    _have_to_wait = true;		
	    }

	    // ################### DISTRIBUTION DISPLAY ######################## //

	    if ((_display % 3) == 2) {
		    

		    if (shared_com[12] == 1){ // Here we have data in distribution panel (graph)

			   int iOffset = (col-3*32)/2;	

			   if (shared_com[10]==0) {
				wclear(aGraphWin);
				mvwprintw(aGraphWin, 1, iOffset," Distribution graph (for selected caller) :");

				void* func;
				memcpy(&func, &shared_com[15], 8);
				char buffer[2048];
				if (demangle(buffer, 2048, func)) {
					buffer[3*32-14] = 0;
					mvwprintw(aGraphWin, 2, iOffset," Call to : %s ", buffer);

				}

			   }	
	
			   // shared_com[10] Item position  2^n step (log2) i.e    3 between 2^3*100 = 800ns and 2^4 = 1600ns
			   // shared_com[11] Percent of calls
			   // shared_com[12] = 1;
		
			   int iY = 0.80*(row/2);
			   int iLength = ((shared_com[11]*0.60)*row)/200;
			   int iHeight = (0.60*row/2);
		
			   mvwaddch(aGraphWin, iY+1, iOffset+shared_com[10]*3, ACS_BTEE); 	 
			   mvwaddch(aGraphWin, iY+1, iOffset+shared_com[10]*3+1, ACS_HLINE);
			   mvwaddch(aGraphWin, iY+1, iOffset+shared_com[10]*3+2, ACS_HLINE);
			   
			   mvwaddch(aGraphWin, iY-iHeight, iOffset+shared_com[10]*3, ACS_TTEE); 	 
			   mvwaddch(aGraphWin, iY-iHeight, iOffset+shared_com[10]*3+1, ACS_HLINE);
			   mvwaddch(aGraphWin, iY-iHeight, iOffset+shared_com[10]*3+2, ACS_HLINE);	

			   for (int i=0; i<iHeight; i++) {
				mvwaddch(aGraphWin, iY-i, iOffset+shared_com[10]*3, ACS_VLINE); 
			   }
			   if (shared_com[10] == 31) {  
				mvwaddch(aGraphWin, iY+1, iOffset+(shared_com[10]+1)*3, ACS_BTEE); 
				mvwaddch(aGraphWin, iY-iHeight, iOffset+(shared_com[10]+1)*3, ACS_TTEE);
				for (int i=0; i<iHeight; i++) {
				  mvwaddch(aGraphWin, iY-i, iOffset+(shared_com[10]+1)*3, ACS_VLINE); 
			     }
			   }

			   if (shared_com[11]>=0) {
				   
				   for (int i=0; i<iLength; i++) {
					mvwaddch(aGraphWin, iY-i, iOffset+shared_com[10]*3+1, ACS_CKBOARD); 
					mvwaddch(aGraphWin, iY-i, iOffset+shared_com[10]*3+2, ACS_CKBOARD); 
				   }
	
				   if (shared_com[11]!=0) mvwprintw(aGraphWin, iY+2, iOffset+shared_com[10]*3+1 ,"%2d", (shared_com[11]==100?99:shared_com[11]));
				   mvwprintw(aGraphWin, iY+3, iOffset+shared_com[10]*3+1 ,"%02d", shared_com[10]);


			   } else {
				   mvwaddch(aGraphWin, iY, iOffset+shared_com[10]*3+1, ACS_BOARD); 
				   mvwaddch(aGraphWin, iY, iOffset+shared_com[10]*3+2, ACS_BOARD);
			   }

			   if (shared_com[10] == 31) {   
			     wrefresh(aGraphWin);
			   }

			   shared_com[12] = 0; // ask for next data		

		    } 

			
		     
		    if (shared_com[13] == 1) {

			   wclear(aGraphWin);
			   mvwprintw(aGraphWin, 1,0," Distribution graph [NO CALLER SELECTED]");	
			   wrefresh(aGraphWin);	

		    }
		    

	    }

	    // Switch kind of display
	    if (_display!=shared_com[8]) {	 

		    _display = shared_com[8];	

		    if ((_display % 3)==0) {
		    	 getmaxyx(stdscr,row,col);
		    	 
			 wresize(aOutputWin, row/2, col);
			 mvwin(aOutputWin, row/2+1, 0);

			 wresize(aDisplayWin, row/2, col);			

			 wresize(aGraphWin, 1, col);
			 mvwin(aGraphWin, row+1, 0);
		      

		    } else if ((_display % 3)==1) {
			 getmaxyx(stdscr,row,col);

		    	 wresize(aOutputWin, 1, col);
			 mvwin(aOutputWin, row+1, 0);

		    	 wresize(aDisplayWin, row, col);

			 wresize(aGraphWin, 1, col);
			 mvwin(aGraphWin, row+1, 0);

		    } else if ((_display % 3)==2) {
			 getmaxyx(stdscr,row,col);

			 wresize(aOutputWin, 1, col);
			 mvwin(aOutputWin, row+1, 0);

			 wresize(aDisplayWin, row/2, col);

			 wresize(aGraphWin, row/2, col);
			 mvwin(aGraphWin, row/2+1, 0);
		    }
		    
	    }
	    pthread_mutex_unlock (&ncurses_mutex);

	    if (_have_to_wait) {
	       usleep(20000);	
	       _have_to_wait = false;
 	    }	
		
    }	

    close(pipe_fds[0]);
    close(pipe_fds[1]);

    if (!stop_ncurses) endwin();	

}

extern "C" bool stopProgram(){
	return shared_com[1]==1;
}

extern "C" void redirect_standard_output() {
   
   int shmid = shmget(IPC_PRIVATE, COMMUNICATION_BUFFER_SIZE + 1, 0660 | IPC_CREAT);	
   
   pipe(pipe_fds);

   switch (fork()) {
      case -1:                      // = error
         perror("fork");
         exit(EXIT_FAILURE);
      case 0: {	                // = child 
	    shared_com = (char*) shmat(shmid, 0, 0);
	    
	    shared_com[0] = 0;
	    shared_com[5] = 0; // Select first item in the list	
	    shared_com[6] = MODE_LIST; // Display mode list
	    shared_com[7] = 0;
         dup2(pipe_fds[0], 0);      // redirect pipe to child's stdin
         setvbuf(stdout, 0, _IONBF, 0);

	    // Calls the screen builder
	    screenBuilder();

         _exit(EXIT_SUCCESS);
      }
      default:                      // = parent
         dup2(pipe_fds[1], 1);      // replace stdout with output to child
         setvbuf(stdout, 0, _IONBF, 0);
	    shared_com = (char*) shmat(shmid, 0, 0);
   }

   // Create thread that will manage the print in buffer from shared mem	
   pthread_create(&output_thread, NULL, &redirected_thread, (void*) 0);
   pthread_create(&key_thread, NULL, &keyboard_thread, (void*) 0);
   pthread_create(&distrib_thread, NULL, &distribution_thread, (void*) 0);		
	
}

// ################################################################## //
// ## STRUCTURES MANAGEMENT    							  ## //
// ################################################################## //

extern "C" int demangle(char* oBuffer, int iSize, void *func){

	int status;
	char aMangled[2048];

	char** data = backtrace_symbols(&func, 1);
	char* aFirst = strchr(data[0],'(');
	char* aLast = strchr(data[0],')');

	int aSize = aLast-aFirst-1;
	if ((aSize<iSize) && (aSize<2048) ) {
		
		strncpy ( aMangled, (char*) (aFirst+1), aSize);
		aMangled[aSize]=0;
		aLast = strrchr( aMangled,'+');
		if (aLast!=0) aLast[0] = 0;

		if (strlen(aMangled)==0) return 0;
		char* aDemangled = abi::__cxa_demangle(aMangled, 0, 0, &status);

		if (status==-2) { // Invalid C++ name
			int dSize = strlen(aMangled);
			dSize = (dSize>iSize?iSize:dSize);
			strncpy(oBuffer,aMangled, dSize);
			oBuffer[dSize]=0;
		} else {
			int dSize = strlen(aDemangled);
			dSize = (dSize>iSize?iSize:dSize);
			strncpy(oBuffer,aDemangled, dSize);
			oBuffer[dSize]=0;
		}			

		return 1;	
	}

	return 0;

}

ITEM_METHOD* findItem(THREAD_CONTEXT* iCtxt, void *func){
	ITEM_METHOD* _start = iCtxt->_first;
	while (_start!=0) {
		if (_start->_func==func) {
			return _start;
		}
		_start = (ITEM_METHOD*) _start->_nextItem;
	}
	return (ITEM_METHOD*) 0;
}

ITEM_METHOD* findItemByName(THREAD_CONTEXT* iCtxt, char* iName){
	ITEM_METHOD* _start = iCtxt->_first;
	while (_start!=0) {
		if (strcmp(_start->_name,iName)==0) {
			return _start;
		}
		_start = (ITEM_METHOD*) _start->_nextItem;
	}
	return (ITEM_METHOD*) 0;
}

THREAD_CONTEXT* addThreadContext(long iThreadId){
	THREAD_CONTEXT* aNew = new THREAD_CONTEXT();
	aNew->_threadid = iThreadId;
	aNew->_stack = new ITEM[MAX_STACK_DEPTH];
	aNew->_stackdepth = 1; // Start at 1 to cumulate spent time at n-1
	if (_lastThreadItem==0) {
		_firstThreadItem = aNew;
		_lastThreadItem = aNew;
	} else {
		_lastThreadItem->_nextItem = (void*) aNew;
		_lastThreadItem = aNew;
	}
	_numberOfThreads++;
	return (THREAD_CONTEXT*) aNew;
}

THREAD_CONTEXT* findThreadContext(long iThreadId){
	THREAD_CONTEXT* _start = _firstThreadItem;
	while (_start!=0) {
		if (_start->_threadid==iThreadId) {
			return _start;
		}
		_start = (THREAD_CONTEXT*) _start->_nextItem;
	}
	return (THREAD_CONTEXT*) 0;
}

THREAD_CONTEXT* getThreadContext(long iThreadPos){
	int count = 0;
	THREAD_CONTEXT* _start = _firstThreadItem;
	while (_start!=0) {
		if (count==iThreadPos) {
			return _start;
		}
		_start = (THREAD_CONTEXT*) _start->_nextItem;
		count++;
	}
	return (THREAD_CONTEXT*) 0;
}

ITEM_CALLER* addCaller(ITEM_METHOD *iItemMethod, void* callsite){

	ITEM_CALLER* aNew = new ITEM_CALLER();
	aNew->_callsite = callsite;
	aNew->_calls = iItemMethod;
	aNew->_name = new char[2048];
	if (demangle(aNew->_name, 2048, callsite)) {
	}
	aNew->_count = 0;
	aNew->_nextItem = 0; 
	aNew->_cumulatedTime = 0;
	aNew->_cumulatedOuterTime = 0;
	aNew->_min = 1000000000L;
	aNew->_max = 0;

	if (iItemMethod->_last_caller==0) {
		iItemMethod->_callers = aNew;
		iItemMethod->_last_caller = aNew;
	} else {
		iItemMethod->_last_caller->_nextItem = (void*) aNew;
		iItemMethod->_last_caller = aNew;
	}
	
	iItemMethod->_numberCallers++;

	return aNew;
}

ITEM_CALLER* findCaller(ITEM_METHOD *iItemMethod, void* callsite){
	ITEM_CALLER* _start = iItemMethod->_callers;
	while (_start!=0) {
		if (_start->_callsite==callsite) {
			return _start;
		}
		_start = (ITEM_CALLER*) _start->_nextItem;
	}
	return (ITEM_CALLER*) 0;
}

extern "C" void recordCallers(ITEM_METHOD *iItemMethod,void *callsite, long iCTime, long iOTime){

	ITEM_CALLER* aItem = findCaller(iItemMethod, callsite);
	if (aItem==0) {
		aItem = addCaller(iItemMethod, callsite);
	}

	aItem->_count++;
	aItem->_touched = 0;
	aItem->_cumulatedTime += iCTime;
	aItem->_cumulatedOuterTime += iOTime;
	
	long aAvg = aItem->_cumulatedTime/aItem->_count;
	if (aItem->_min>aAvg) {
	   aItem->_min = aAvg;
	}
	if (aItem->_max<aAvg) {
	   aItem->_max = aAvg;
	}

	// Distribution
	int i = (int) (log(iCTime/100)/log(2));	
	if (i>31) {
	   i = 31;	
	}
	aItem->_distrib[i]++;	

}

int findCallerByName(ITEM_METHOD *iItemMethod, char* aNameFunc){

	int count=0;
	ITEM_CALLER* _start = iItemMethod->_callers;
	while (_start!=0) {
		//printf(" demangle second [%p] \n", _start->_callsite);
		if (strcmp(aNameFunc,_start->_name)==0) {
			count++;
		}
		_start = (ITEM_CALLER*) _start->_nextItem;
	}
	return count;
}

extern "C" std::vector<ITEM_METHOD*> calls(THREAD_CONTEXT* iCtxt ,char *iFuncName){

	std::vector<ITEM_METHOD*> aCalled;

	ITEM_METHOD* _start = iCtxt->_first;
	while (_start!=0) {

		if (findCallerByName(_start, iFuncName)) {
			aCalled.push_back(_start);
		}

		_start = (ITEM_METHOD*) _start->_nextItem;
	}
	
	return aCalled;

}

std::vector<ITEM_CALLER*> returnMethodCallsByName(ITEM_METHOD *iItemMethod, char* aNameFunc){

	std::vector<ITEM_CALLER*> aResult;	

	ITEM_CALLER* _start = iItemMethod->_callers;
	while (_start!=0) {
		//printf(" demangle second [%p] \n", _start->_callsite);
		if (strcmp(aNameFunc,_start->_name)==0) {
			aResult.push_back(_start); // callers may have same name but != address
		}
		_start = (ITEM_CALLER*) _start->_nextItem;
	}

	return aResult;
}


extern "C" std::vector<ITEM_CALLER*> returnMethodCalls(THREAD_CONTEXT* iCtxt ,char *iFuncName){

	std::vector<ITEM_CALLER*> aCallers;

	ITEM_METHOD* _start = iCtxt->_first;
	while (_start!=0) {

		// A method can call at several places the same method
		std::vector<ITEM_CALLER*> aICallers = returnMethodCallsByName(_start, iFuncName);
		for (int i=0; i<aICallers.size(); i++) {
			aCallers.push_back(aICallers[i]);
		}

		_start = (ITEM_METHOD*) _start->_nextItem;
	}
	
	return aCallers;

}

// ################################################################ //
// ## Here display management, we will say to the screen builder ## //
// ## what to display ....							     ## //
// ################################################################ //

// TODO create utils file
extern "C" std::string string_format(const std::string fmt, ...) {
    int size = 100;
    std::string str;
    va_list ap;
    while (1) {
        str.resize(size);
        va_start(ap, fmt);
        int n = vsnprintf((char *)str.c_str(), size, fmt.c_str(), ap);
        va_end(ap);
        if (n > -1 && n < size) {
            str.resize(n);
            return str;
        }
        if (n > -1)
            size = n + 1;
        else
            size *= 2;
    }
    return str;
}

extern "C" std::string timeDisplay(long long iTime){

	if (iTime<0) { // Error
 		return string_format("[N/A]",iTime);	
	} else if (iTime<1000L) {
		return string_format("%lldns",iTime);	
	} else if (iTime<1000000L) {
		return string_format("%3.2fus",iTime/1000.0);
	} else if (iTime<1000000000L) {
		return string_format("%3.2fms",iTime/1000000.0);
	} else {
		return string_format("%3.2fs",iTime/1000000000.0);
	}

}

extern "C" void remoteClearScreen(){
	shared_com[0]=5;
	usleep(100000); // Sleep 100 ms to clean screen
}

extern "C" int isCalling(std::vector<ITEM_METHOD*>& called, char* iName){
	int count = 0;	
	for (int i=0; i<called.size();i++){
		if (strcmp(called[i]->_name,iName)==0) {
			count++;		
		}
	}
	return count;
}

extern "C" int isCalled(ITEM_METHOD* called, char* iName){
	return findCallerByName(called, iName);
}

extern "C" std::vector<ITEM_CALLER*> findPrimaryCallers(THREAD_CONTEXT* iCtxt){

	std::vector<ITEM_CALLER*> aPrimaryCaller;

	ITEM_METHOD* _start = iCtxt->_first;
	while (_start!=0) {

		if ((_start->_callers!=0) && (_start->_callers->_nextItem==0)) { // only one caller
			char* aName = _start->_callers->_name;
			ITEM_METHOD* aItem = findItemByName(iCtxt, aName);
			if (aItem==0) {
				aPrimaryCaller.push_back(_start->_callers);
			}
		}

		_start = (ITEM_METHOD*) _start->_nextItem;
	}

	return aPrimaryCaller;

}

struct set_comp_str {
  bool operator() (const char* lhs, const char* rhs) const
  { return strcmp(lhs,rhs)<0; }
};
 

extern "C" PATH* findPathTree(THREAD_CONTEXT* iCtxt, std::set<char*,set_comp_str>& iPrevious, ITEM_CALLER* iEntryPoint) {
	
	PATH* aPath = new PATH();
	aPath->_method = iEntryPoint;

	std::vector<ITEM_CALLER*> aCalls;
	if (iEntryPoint->_touched) {
		aCalls = iEntryPoint->_callslist;
	} else {
		aCalls = returnMethodCalls( iCtxt , iEntryPoint->_calls->_name);
		iEntryPoint->_callslist = aCalls;
		iEntryPoint->_touched = 1;
	}
	
	if (aCalls.size()==0) {
		return aPath;
	} else {
		for (int i=0; i<aCalls.size(); i++){
			
			std::set<char*,set_comp_str> aPrev = iPrevious;
			std::set<char*,set_comp_str>::iterator it = iPrevious.find(aCalls[i]->_calls->_name); // Prevent recursion
			if (it==iPrevious.end()) {
				aPrev.insert(aCalls[i]->_calls->_name);
				PATH* aTmpResult = findPathTree(iCtxt, aPrev, aCalls[i]);
				aPath->_path.push_back(aTmpResult);
			}

		}
	}

	return aPath;

}

ITEM_CALLER* _reserved_caller;

extern "C" int printPathTree(PATH* iPath, int iDepth,  int& iLineCount, int& iMain){

	if (iLineCount>shared_com[7]) return 1;

	char aSpace[2048];
	memset(aSpace, 32, iDepth);
	aSpace[iDepth]=0;

	if (shared_com[0]==4) {
		remoteClearScreen();
		return 1;
	}

	long count = (int) iPath->_method->_count;
	long long cumulT = iPath->_method->_cumulatedTime;
	long long cumulOT = iPath->_method->_cumulatedOuterTime;
	
	if (iLineCount==shared_com[5]+1) {
		_reserved_caller = iPath->_method;
	}	

	int aPos = 0;
	if (needDisplay(iLineCount, aPos)) {
		int size = snprintf((char*) (shared_com+SHARED_CHANNEL), COMMUNICATION_BUFFER_SIZE-(SHARED_CHANNEL+1), 
			"%14p | %09lu | %8s | %8s | %8s |   %s%s\n",  iPath->_method->_callsite,
			 count,
			 timeDisplay(cumulT).c_str(),
			 timeDisplay(cumulT/count).c_str(),
			 timeDisplay((cumulT-cumulOT)/count).c_str(),
			 aSpace, 
			 iPath->_method->_calls->_name);
		shared_com[2] = iLineCount; 
		((char*) (shared_com+SHARED_CHANNEL))[size]=0;
		shared_com[0] = 3;
	}
	while (shared_com[0]==3) {
		usleep(10000);
	}
	iLineCount++;

	std::vector<PATH*> aPath = iPath->_path;
	std::sort (aPath.begin(), aPath.end(), CMP_GREAT_ADAPT<PATH>);
	for (int i=0; i<aPath.size(); i++) {
		if ((i==0) && (iMain==1)) { shared_com[9] = 1; } else { shared_com[9] = 0; }
		if (printPathTree(aPath[i], iDepth+1, iLineCount, iMain)) return 1;
	}
	if (aPath.size()==0) { // No more PATH == end of main path
		iMain = 0;
	}
	
	return 0;

}

extern "C" void clearPathTree(PATH* iPath){
	
	for (int i=0; i<iPath->_path.size(); i++) {
		clearPathTree(iPath->_path[i]);
	}
	
	delete iPath;

}

// This thread transmit distribution data
extern "C" void* distribution_thread(void* args) {
	
	bool stop = false;

	while (!stop) {

		ITEM_CALLER* aWorkOn = _reserved_caller;

		if (aWorkOn!=0) {
			memcpy(&shared_com[15], &aWorkOn->_calls->_func, 8);
		}

		int count=0;
		while (count<32) {

			while ( shared_com[12] ==1)  usleep(10000);

			if (aWorkOn!=0) {
			   shared_com[10] = count;
			   shared_com[11] = (100*aWorkOn->_distrib[shared_com[10]])/aWorkOn->_count;
			   shared_com[12] = 1;
			} else {
			   shared_com[10] = count;
			   shared_com[11] = -1;
			   shared_com[12] = 1;
			}

			count++;
		}

	     int status = shared_com[0];
	     if (status==2) { // Stop the process
		    stop = true;
	     }

	}

}

extern "C" void displayLongestPaths(THREAD_CONTEXT* iCtxt) {

	std::vector<ITEM_CALLER*> aPCallers = findPrimaryCallers(iCtxt);
	std::sort (aPCallers.begin(), aPCallers.end(), CMP_GREAT_ADAPT<ITEM_CALLER>);

	int lineCount = 0;
	for (int i=0; i<aPCallers.size(); i++) {

		std::set<char*,set_comp_str> aStart;
		aStart.insert(aPCallers[i]->_calls->_name);
		PATH* aPath = findPathTree(iCtxt,aStart, aPCallers[i]);

		
		
		if (i>shared_com[7]) return; // Transmit screen size
		
		if (shared_com[0]==4) {
			remoteClearScreen();
			return;
		}

		long count = (int) aPCallers[i]->_count;
		long long cumulT = aPCallers[i]->_cumulatedTime;
		long long cumulOT = aPCallers[i]->_cumulatedOuterTime;

		if (lineCount==shared_com[5]+1) {
			_reserved_caller = 0;
		}		

		int aPos = 0;
		if (needDisplay(lineCount, aPos)) {

			if (i==0) { shared_com[9] = 1; } else { shared_com[9] = 0; }
			int size = snprintf((char*) (shared_com+SHARED_CHANNEL), COMMUNICATION_BUFFER_SIZE-(SHARED_CHANNEL+1), 
							"%14p | %09lu | %8s | %8s | %8s | P %s \n", aPCallers[i]->_callsite,
							count,	 
							timeDisplay(cumulT).c_str(),
							timeDisplay(cumulT/count).c_str(),
							timeDisplay((cumulT-cumulOT)/count).c_str(), 
							aPCallers[i]->_name );

			shared_com[2] = lineCount; 
			((char*) (shared_com+SHARED_CHANNEL))[size]=0;
			shared_com[0] = 3;

		}
		while (shared_com[0]==3) {
			usleep(10000);
		}
		lineCount++;

		int aMain = (i==0?1:0);
		if (printPathTree(aPath, 1, lineCount, aMain)==1) break;

		clearPathTree(aPath);

	}

}

extern "C" void displayContextCalls(THREAD_CONTEXT* iCtxt) {

	_reserved_caller = 0;

	// Create a list to display sorted methods by call
	std::vector<ITEM_METHOD> aList;	
	ITEM_METHOD* _start = iCtxt->_first;
	while (_start!=0) {
		aList.push_back(*_start);
		_start = (ITEM_METHOD*) _start->_nextItem;
	}
	std::sort (aList.begin(), aList.end(), std::greater<ITEM_METHOD>()); // sort

	int aItemsCount = 0;

	// Selected item
	if (shared_com[5]<0) shared_com[5] = 0;
	if (shared_com[5]>=aList.size()) shared_com[5] = aList.size()-1;

	std::vector<ITEM_METHOD*> aWhoCalls;
	ITEM_METHOD* aMethod = 0;
	if ((shared_com[5]>=0) && (shared_com[5]<aList.size())) {
		aMethod = &aList[shared_com[5]];
		aWhoCalls = calls(iCtxt , aList[shared_com[5]]._name);
	}

	for (int i=0; i<aList.size(); i++) {

		while (shared_com[0]==3) {
			usleep(10000);
		}
		
		if (i>shared_com[7]) return; // Transmit screen size
		
		if (shared_com[0]==4) {
			remoteClearScreen();
			return;
		}

		if (strlen(aList[i]._name)==0) {
		  //printf("%d : [no name] \n",(int) _start->_count);

		  int size = snprintf((char*) (shared_com+SHARED_CHANNEL), COMMUNICATION_BUFFER_SIZE-(SHARED_CHANNEL+1), 
									"%09d | %9s | %8s | %8s | %6lu | %5lu |[no name] \n", 
									(int) aList[i]._count,
									timeDisplay(aList[i]._cumulatedTime).c_str(),
									timeDisplay(aList[i]._cumulatedTime/aList[i]._count).c_str(),
									timeDisplay(aList[i]._cumulatedOuterTime/aList[i]._count).c_str(),
									aList[i]._numberCallers,
									0L
									); 
		  shared_com[2] = i+1; 
		  ((char*) (shared_com+SHARED_CHANNEL))[size]=0;

		  shared_com[0] = 3;

		} else {

		  long count = (int) aList[i]._count;
		  long long cumulT = aList[i]._cumulatedTime;
		  long long cumulOT = aList[i]._cumulatedOuterTime;	

		  std::vector<ITEM_METHOD*> aCalls = calls(iCtxt , aList[i]._name);		

		  char c = ' ';
		  int nCalling = isCalling(aWhoCalls, aList[i]._name);
		  if (nCalling==1) c='*';
		  if (nCalling>1) c='S';

		  char c2 = ' ';
		  int nCalled = isCalled(aMethod, aList[i]._name);
		  if (nCalled==1) c2='*';
		  if (nCalled>1) c2='S';

		  int size = snprintf((char*) (shared_com+SHARED_CHANNEL), COMMUNICATION_BUFFER_SIZE-(SHARED_CHANNEL+1), "%09lu | %9s | %8s | %8s | %6lu%c| %5lu%c|%s \n", 
					 count,
					 timeDisplay(cumulT).c_str(), 
					 timeDisplay(cumulT/count).c_str(),
					 timeDisplay((cumulT-cumulOT)/count).c_str(),
					 aList[i]._numberCallers, 
					 c,
					 aCalls.size(),
					 c2,	
					 aList[i]._name); 	
		  shared_com[2] = i+1; 		
		  ((char*) (shared_com+10))[size]=0;
		  //printf("%d : %s \n",(int) _start->_count, aDemangled);

		  shared_com[0] = 3;
			
		}
	
	} 

}

int aCTxtNum = 0;

extern "C" void* redirected_thread(void* iArg){

	stop_output_thread = false;
	
	shared_com[0]=0;

	// Here we start to read the ouput data
	while (!stop_output_thread) {	

		if ( shared_com[1]==1 ) { stop_output_thread = true; }
		
		THREAD_CONTEXT* aCtxt = getThreadContext(aCTxtNum);
	
		if (aCtxt) { 
			if (shared_com[6]==MODE_LIST) {
				displayContextCalls(aCtxt);
			} else if (shared_com[6]==MODE_PATH) {
				displayLongestPaths(aCtxt);
			}
		}
		while (shared_com[0]==3) {
			usleep(10000);
		}
		shared_com[0] = 10;	
		usleep(20000); // this threads controls what to display to the screen
		
	}

	return 0;
}

extern "C" void* keyboard_thread(void* iArg){

	shared_com[4] = _orderMethod;  
	while (!stop_output_thread) {
		
		int c = getchar();
 		if (c==27) { // TODO change that by lib calls
			c = getchar();
			if (c==91) {
				c = getchar();
				if (c==65) {
					shared_com[5]--;
				} else if (c==66) {
					shared_com[5]++;
				} else if (c==67) {
					aCTxtNum++;
					if (aCTxtNum>=_numberOfThreads) {
					   aCTxtNum = _numberOfThreads-1;
					}
					shared_com[3]=aCTxtNum;
					shared_com[0]=4;
				} else if (c==68) {
					aCTxtNum--;
					if (aCTxtNum<0) {
					   aCTxtNum = 0;
					}
				     shared_com[3]=aCTxtNum;
					shared_com[0]=4;
				}
			}
		} else if (c=='n'){
			_orderMethod = ORDER_COUNT;
		} else if (c=='c'){
			_orderMethod = ORDER_CPU;
		} else if (c=='i'){
			_orderMethod = ORDER_INNER;
		} else if (c=='a'){
			_orderMethod = ORDER_AVG;
		} else if (c=='m'){
			shared_com[6]= (shared_com[6]+1) % 2;
			shared_com[0]=4; // Stops remote screen display
		} else if (c=='d'){
			shared_com[8]= shared_com[8]+1; // Switch display or not
			shared_com[0]=4; // Stops remote screen display
		}

		shared_com[4] = _orderMethod;

	}

}

extern "C" void stopProfiler() {

	//restoreStdOut();
    
	printf("Profiler stopping ...\n");

	shared_com[0] = 2;	// Stop the screen builder process

	close(pipe_fds[0]);
	close(pipe_fds[1]);	

}

// We catch interruptions to stop properly the profiler
void intHandler(int dummy) {
    // here we pass the info !! need to stop other process 	 
    stopProfiler();
    exit(0);
}

struct sigaction _hdlErrorStruct;
struct sigaction _hdlTermStruct;

extern "C" void handlingTerminate(int iSignal, siginfo_t *iInfo, void *arg){
    printf("Program terminated...");
    stopProfiler();
    exit(0);	
}

extern "C" void handlingErrorSignals(int iSignal, siginfo_t *iInfo, void *arg){

    // Set an error message

    stopProfiler();
    exit(0);

}

extern "C" void manageSignals(){

	// Catch interruptions
	signal(SIGINT, intHandler);

	_hdlErrorStruct.sa_handler = 0;
	_hdlErrorStruct.sa_sigaction = handlingErrorSignals;
	_hdlErrorStruct.sa_mask = 0;
	_hdlErrorStruct.sa_flags = SA_SIGINFO;
	_hdlErrorStruct.sa_restorer = 0;

	sigaction(SIGSEGV, &_hdlErrorStruct, 0);
	sigaction(SIGILL, &_hdlErrorStruct, 0);
	sigaction(SIGFPE, &_hdlErrorStruct, 0);


	_hdlTermStruct.sa_handler = 0;
	_hdlTermStruct.sa_sigaction = handlingTerminate;
	_hdlTermStruct.sa_mask = 0;
	_hdlTermStruct.sa_flags = SA_SIGINFO;
	_hdlTermStruct.sa_restorer = 0;

	sigaction(SIGTERM, &_hdlTermStruct, 0);

}

extern "C" void initProfiler(void *func){

	init = 1;

	printf("Profiler initialization : entry pointer [%p]... \n", func );

	redirect_standard_output();

	manageSignals();

	_entryPointer = func;

}

extern "C" int checkMain(void * func){

	char** data = backtrace_symbols(&func, 1);
	printf("%s\n",data[0]);
	if (strstr( data[0], "main" )!=0) {
		printf("Entering main ...\n");
		main_found = 1;
		return 1;
	}
	return 0;
}

extern "C" timespec diff(timespec& start, timespec& end)
{
	timespec temp;
	if ((end.tv_nsec-start.tv_nsec)<0) {
		temp.tv_sec = end.tv_sec-start.tv_sec-1;
		temp.tv_nsec = 1000000000L+end.tv_nsec-start.tv_nsec;
	} else {
		temp.tv_sec = end.tv_sec-start.tv_sec;
		temp.tv_nsec = end.tv_nsec-start.tv_nsec;
	}
	return temp;
}



extern "C"  void __cyg_profile_func_enter(void *func, void *callsite){

   timespec time1, time2;
   clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time1);	

   if (!init) {	 
	 initProfiler(func);	
   }

   if (!main_found) {
	 if (!checkMain(func)) return;
   }

   if (stopProgram()) {
	intHandler(0);
   }	

   long aThreadID = (long) pthread_self();
   
   THREAD_CONTEXT *aCtxt = findThreadContext(aThreadID);
   if (aCtxt==0) {
	 aCtxt = addThreadContext(aThreadID);	
   }

   ITEM *aItem = &aCtxt->_stack[aCtxt->_stackdepth];
   aItem->_func = func;
   aItem->_from = callsite;
   aItem->_spent = 0; // Initialize spent time in +1 functions
   aItem->_outerTime = 0;
   clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time2);
   aItem->_time = time2;

   // Cumulate time spent     
   aItem = &aCtxt->_stack[aCtxt->_stackdepth-1];
   aItem->_spent += diff( time1, time2).tv_nsec;
 	
   aCtxt->_stackdepth++;  

}

extern "C" void __cyg_profile_func_exit(void *func, void *callsite){

   timespec time1, time2;
   clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time1);	
	
   int ret = 0L;
   GET_EBX(ret);

   if (stopProgram()) {
	intHandler(0);
   }
	
   if (!main_found) {
	return;
   }

   long aThreadID = (long) pthread_self();
   THREAD_CONTEXT *aCtxt = findThreadContext(aThreadID);	

   ITEM *aItem = &aCtxt->_stack[aCtxt->_stackdepth-1];
   if (aItem->_func==func) {	

	  ITEM_METHOD *aItemMethod = findItem(aCtxt, func); 
	  timespec _time_start = aItem->_time;
	  long cTime = diff(_time_start, time1).tv_nsec - aItem->_spent;
	  if (aItemMethod==0) {

		   aItemMethod = new ITEM_METHOD();
		   aItemMethod->_func = func;
			
		   aItemMethod->_name = new char[2048];
		   if (demangle(aItemMethod->_name, 2048, func)) {
		   }
	
		   aItemMethod->_count = 1;
		   aItemMethod->_cumulatedTime = cTime ;
		   aItemMethod->_cumulatedOuterTime = aItem->_outerTime;	
		   aItemMethod->_spent =  aItem->_spent;
		   //aItemMethod->_previousItem = (void*) aCtxt->_recorded;
		   aItemMethod->_nextItem = 0;

		   // Callers
		   //ITEM_CALLER *_callers;
		   //ITEM_CALLER *_last_caller;
		   aItemMethod->_callers = 0;	
		   aItemMethod->_last_caller = 0;
		   aItemMethod->_numberCallers = 0;

		   if (aCtxt->_recorded==0) {
			aCtxt->_first = aItemMethod;
			aCtxt->_recorded = aItemMethod;
		   } else {
			aCtxt->_recorded->_nextItem = (void*) aItemMethod;
			aCtxt->_recorded = (ITEM_METHOD*) aItemMethod;
	   	   }	
	 	
	  } else {
		   aItemMethod->_count++;
		   aItemMethod->_cumulatedTime += cTime ;
		   aItemMethod->_cumulatedOuterTime += aItem->_outerTime;		
		   aItemMethod->_spent =  aItem->_spent;
	  }
	 
	  // Here adding callers information
	  recordCallers(aItemMethod,callsite,cTime,aItem->_outerTime);	

       aCtxt->_stackdepth--;

	  aItem = &aCtxt->_stack[aCtxt->_stackdepth-1];
	  aItem->_outerTime += cTime;
	  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time2); // TODO cumulated spent time
	  aItem->_spent += diff(time1, time2).tv_nsec ;

   }	

   if (func==_entryPointer){
      printf("Profiler ends [%p]\n",func);
	 stopProfiler();
   }

   SET_EBX(ret);

}


	
