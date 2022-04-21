#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

#define NULL (void*)0

int MatchFileName(const char* path, const char* file_name) {
  if (path == NULL || file_name == NULL) return 0;

  const char* cur = path + strlen(path) - 1;
  for (; cur >= path; cur--) {
    if (*cur == '/')
      break;
  }
  cur++;
  if (strcmp(cur, file_name) == 0) 
    return 1;
  else
    return 0;
}

void Find(const char* path, const char* file_name) {
  if (path == NULL || file_name == NULL) 
    return;

  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0) {
    fprintf(2, "cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(2, "cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type) {
    case T_FILE:
      if (MatchFileName(path, file_name)) {
        printf("%s\n", path); 
      }
      break;
    case T_DIR:
      if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
        fprintf(2, "path too long\n");
        break;
      }

      strcpy(buf, path);
      p = buf + strlen(path);
      *p++ = '/';
      while(read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0 
            || strcmp(de.name, ".") == 0 
            || strcmp(de.name, "..") == 0) 
          continue;
        memcpy(p, de.name, DIRSIZ);
        p[DIRSIZ] = '\0';
        // printf("log searching: %s\n", buf);
        Find(buf, file_name);
        *p = '\0';
      }
      break;
  }
  close(fd);
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    fprintf(2, "Usage: find path file_name\n");
  }
  Find(argv[1], argv[2]);
  exit(0);
}
