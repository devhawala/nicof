#include <stdio.h>
 
static void usage(char *pname) {
  printf("Usage: %s [STACK] mode {QDISK|LIST [fn [ft [fm]]]}\n", pname);
  printf("  with mode : COMMAND | CONSOLE | FUNCTION\n");
  __exit();
}
 
int main(int argc, char* argv[]) {
  char *pname = argv[0];
  int stacked = 0;
  int qdisk = 0;
  int mode = CMS_COMMAND;
  char *fn = "*";
  char *ft = "*";
  char *fm = "A";
 
  int base = 1;
  if ((argc - base) < 2) { usage(pname); }
  if (strcmp(argv[base], "STACK") == 0) {
    stacked = 1;
    base++;
  }
  if ((argc - base) < 2) { usage(pname); }
  if (strcmp(argv[base], "COMMAND") == 0) {
    mode = CMS_COMMAND;
  } else if (strcmp(argv[base], "CONSOLE") == 0) {
    mode = CMS_CONSOLE;
  } else if (strcmp(argv[base], "FUNCTION") == 0) {
    mode = CMS_FUNCTION;
  } else {
    usage(pname);
  }
  base++;
  if (strcmp(argv[base], "QDISK") == 0) {
    qdisk = 1;
  } else if (strcmp(argv[base], "LIST") != 0) {
    usage(pname);
  }
  base++;
  if (base < argc) { fn = argv[base++]; }
  if (base < argc) { ft = argv[base++]; }
  if (base < argc) { fm = argv[base++]; }
 
  char cmd[120];
  if (qdisk) {
    sprintf(cmd, "QUERY DISK%s", (stacked) ? " (FIFO" : "");
  } else {
    sprintf(cmd, "LISTFILE %s %s %s ( LABEL%s",
      fn, ft, fm, (stacked) ? " FIFO" : "");
  }
 
  printf(">> executing '%s' as %s (= %d)\n", cmd,
    (mode == CMS_COMMAND) ? "CMS_COMMAND"
    : (mode == CMS_CONSOLE) ? "CMS_CONSOLE" : "CMS_FUNCTION",
    mode);
 
  int rc = CMScommand(cmd, mode);
  printf("** RC = %d\n", rc);
  if (stacked) {
    char buffer[133];
    printf(">>>>> begin stacked data\n");
    while(CMSstackQuery()) {
      int len = CMSconsoleRead(buffer);
      buffer[len] = '\0';
      printf("%s\n", buffer);
    }
    printf(">>>>> end stacked data\n");
  }
}
