// stacktrace.h (c) 2008, Timo Bingmann from http://idlebox.net/
// published under the WTFPL v2.0

#ifndef _STACKTRACE_H_
#define _STACKTRACE_H_

#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <execinfo.h>
#include <cxxabi.h>


static inline const char* stacktrace_caller_frame_desc(const char *prefix, int offset = 1) {
  // storage array for stack trace address data
  void* addrlist[offset+2];
  int listlen = sizeof(addrlist) / sizeof(void*);
  
  // retrieve current stack addresses
  int addrlen = backtrace(addrlist, listlen);
  if (addrlen < listlen) {
    fprintf(stderr, "  <empty, possibly corrupt>\n");
    return nullptr;
  }
  
  // resolve addresses into strings containing "filename(function+address)",
  // this array must be free()-ed
  char** symbollist = backtrace_symbols(addrlist, addrlen);

  // allocate string which will be filled with the demangled function name
  size_t funcnamesize = 256;
  char* funcname = (char*)malloc(funcnamesize);
  
  char *begin_name = 0, *begin_offset = 0, *end_offset = 0;

  char *caller_symbol = symbollist[listlen-1] + 59;

  // find parentheses and +address offset surrounding the mangled name:
  // ./module(function+0x15c) [0x8048a6d]
  for (char *p = caller_symbol; *p; ++p) {
    if (*p == '(')
      begin_name = p;
    else if (*p == '+')
      begin_offset = p;
    else if (*p == ')' && begin_offset) {
      end_offset = p;
      break;
    }
  }
  
  const int desclen = 512;
  char *desc = (char *)malloc(desclen);
  char *desc_str = desc;
  if (prefix) {
    desc += snprintf(desc, desclen, "[%s] ", prefix);
  }

  if (begin_name && begin_offset && end_offset && begin_name < begin_offset) {
    *begin_name++ = '\0';
    *begin_offset++ = '\0';
    *end_offset = '\0';

    // mangled name is now in [begin_name, begin_offset) and caller
    // offset in [begin_offset, end_offset). now apply
    // __cxa_demangle():

    int status;
    char* ret = abi::__cxa_demangle(begin_name, funcname, &funcnamesize, &status);
    if (status == 0) {
      funcname = ret; // use possibly realloc()-ed string
      snprintf(desc, desclen, "%s+%s", funcname, begin_offset);
    } else {
      // demangling failed. Output function name as a C function with
      // no arguments.
      snprintf(desc, desclen, "%s()+%s", begin_name, begin_offset);
    }
  } else {
    // couldn't parse the line? print the whole line.
    snprintf(desc, desclen, "%s", caller_symbol);
  }
  
  free(funcname);
  free(symbollist);
  
  return desc_str;
}

/** Print a demangled stack backtrace of the caller function to FILE* out. */
static inline void print_stacktrace(FILE *out = stdout, unsigned int max_frames = 63) {
    fprintf(out, "stack trace:\n");

    // storage array for stack trace address data
    void* addrlist[max_frames+1];

    // retrieve current stack addresses
    int addrlen = backtrace(addrlist, sizeof(addrlist) / sizeof(void*));

    if (addrlen == 0) {
      fprintf(out, "  <empty, possibly corrupt>\n");
      return;
    }

    // resolve addresses into strings containing "filename(function+address)",
    // this array must be free()-ed
    char** symbollist = backtrace_symbols(addrlist, addrlen);

    // allocate string which will be filled with the demangled function name
    size_t funcnamesize = 256;
    char* funcname = (char*)malloc(funcnamesize);

    // iterate over the returned symbol lines. skip the first, it is the
    // address of this function.
    for (int i = 1; i < addrlen; i++) {
      char *begin_name = 0, *begin_offset = 0, *end_offset = 0;

      // find parentheses and +address offset surrounding the mangled name:
      // ./module(function+0x15c) [0x8048a6d]
      for (char *p = symbollist[i]; *p; ++p) {
          if (*p == '(')
            begin_name = p;
          else if (*p == '+')
            begin_offset = p;
          else if (*p == ')' && begin_offset) {
            end_offset = p;
          break;
          }
      }

      if (begin_name && begin_offset && end_offset && begin_name < begin_offset) {
          *begin_name++ = '\0';
          *begin_offset++ = '\0';
          *end_offset = '\0';

          // mangled name is now in [begin_name, begin_offset) and caller
          // offset in [begin_offset, end_offset). now apply
          // __cxa_demangle():

          int status;
          char* ret = abi::__cxa_demangle(begin_name, funcname, &funcnamesize, &status);
          if (status == 0) {
            funcname = ret; // use possibly realloc()-ed string
            fprintf(out, "  %s : %s+%s\n", symbollist[i], funcname, begin_offset);
          } else {
            // demangling failed. Output function name as a C function with
            // no arguments.
            fprintf(out, "  %s : %s()+%s\n", symbollist[i], begin_name, begin_offset);
          }
      } else {
          // couldn't parse the line? print the whole line.
          fprintf(out, "  %s\n", symbollist[i]);
      }
    }

    free(funcname);
    free(symbollist);
}

#endif // _STACKTRACE_H_

