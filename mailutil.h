extern bool markAsRead (const char *imapServer, const char *username, const char *password, const char *mailbox);
extern bool smtpSend (const char *toAddress, const char *object, const char *message);
extern int  imapGetUnseen (const char* imapServer, const char*username, const char* password, 
   const char *mailbox, const char *path, char *gribFileName, size_t maxLen);
