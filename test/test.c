#include <stdio.h>

int check_password(const char *input) {
  if (input[0] == 's' && input[1] == 'e' && input[2] == 'c' &&
      input[3] == 'r' && input[4] == 'e' && input[5] == 't') {
    return 1;
  }
  return 0;
}

int main(void) {
  const char *secret = "MySecretKey123";
  printf("Hello from ORK-NEW test!\n");
  printf("Secret: %s\n", secret);

  if (check_password("secret")) {
    printf("Access granted\n");
  } else {
    printf("Access denied\n");
  }

  return 0;
}
