// RUN: cconv-standalone -base-dir=%S -alltypes -output-postfix=checkedALL2 %s %S/arrofstructsafemulti1.c
// RUN: cconv-standalone -base-dir=%S -output-postfix=checkedNOALL2 %s %S/arrofstructsafemulti1.c
//RUN: %clang -c %S/arrofstructsafemulti1.checkedNOALL2.c %S/arrofstructsafemulti2.checkedNOALL2.c
//RUN: FileCheck -match-full-lines -check-prefixes="CHECK_NOALL" --input-file %S/arrofstructsafemulti2.checkedNOALL2.c %s
//RUN: FileCheck -match-full-lines -check-prefixes="CHECK_ALL" --input-file %S/arrofstructsafemulti2.checkedALL2.c %s
//RUN: rm %S/arrofstructsafemulti1.checkedALL2.c %S/arrofstructsafemulti2.checkedALL2.c
//RUN: rm %S/arrofstructsafemulti1.checkedNOALL2.c %S/arrofstructsafemulti2.checkedNOALL2.c


/*********************************************************************************/

/*This file tests three functions: two callers bar and foo, and a callee sus*/
/*In particular, this file tests: how the tool behaves when there is an array
of structs*/
/*For robustness, this test is identical to arrofstructprotosafe.c and arrofstructsafe.c except in that
the callee and callers are split amongst two files to see how
the tool performs conversions*/
/*In this test, foo, bar, and sus will all treat their return values safely*/

/*********************************************************************************/


typedef unsigned long size_t;
#define NULL 0
extern _Itype_for_any(T) void *calloc(size_t nmemb, size_t size) : itype(_Array_ptr<T>) byte_count(nmemb * size);
extern _Itype_for_any(T) void free(void *pointer : itype(_Array_ptr<T>) byte_count(0));
extern _Itype_for_any(T) void *malloc(size_t size) : itype(_Array_ptr<T>) byte_count(size);
extern _Itype_for_any(T) void *realloc(void *pointer : itype(_Array_ptr<T>) byte_count(1), size_t size) : itype(_Array_ptr<T>) byte_count(size);
extern int printf(const char * restrict format : itype(restrict _Nt_array_ptr<const char>), ...);
extern _Unchecked char *strcpy(char * restrict dest, const char * restrict src : itype(restrict _Nt_array_ptr<const char>));

struct general { 
    int data; 
    struct general *next;
	//CHECK: struct general *next;
};

struct warr { 
    int data1[5];
	//CHECK_NOALL: int data1[5];
	//CHECK_ALL: int data1 _Checked[5];
    char *name;
	//CHECK: _Ptr<char> name;
};

struct fptrarr { 
    int *values; 
	//CHECK: _Ptr<int> values; 
    char *name;
	//CHECK: _Ptr<char> name;
    int (*mapper)(int);
	//CHECK: _Ptr<int (int )> mapper;
};

struct fptr { 
    int *value; 
	//CHECK: _Ptr<int> value; 
    int (*func)(int);
	//CHECK: _Ptr<int (int )> func;
};  

struct arrfptr { 
    int args[5]; 
	//CHECK_NOALL: int args[5]; 
	//CHECK_ALL: int args _Checked[5]; 
    int (*funcs[5]) (int);
	//CHECK_NOALL: int (*funcs[5]) (int);
	//CHECK_ALL: _Ptr<int (int )> funcs _Checked[5];
};

int add1(int x) { 
    return x+1;
} 

int sub1(int x) { 
    return x-1; 
} 

int fact(int n) { 
    if(n==0) { 
        return 1;
    } 
    return n*fact(n-1);
} 

int fib(int n) { 
    if(n==0) { return 0; } 
    if(n==1) { return 1; } 
    return fib(n-1) + fib(n-2);
} 

int zerohuh(int n) { 
    return !n;
}

int *mul2(int *x) { 
	//CHECK: _Ptr<int> mul2(_Ptr<int> x) { 
    *x *= 2; 
    return x;
}

struct general ** sus(struct general * x, struct general * y) {
	//CHECK: struct general ** sus(struct general *x, struct general *y) {
x = (struct general *) 5; 
	//CHECK: x = (struct general *) 5; 
        struct general **z = calloc(5, sizeof(struct general *));
	//CHECK: struct general **z = calloc<struct general *>(5, sizeof(struct general *));
        struct general *curr = y;
	//CHECK: struct general *curr = y;
        int i;
        for(i = 0; i < 5; i++) { 
            z[i] = curr; 
            curr = curr->next; 
        } 
        
return z; }
