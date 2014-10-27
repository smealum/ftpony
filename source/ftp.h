#ifndef FTP_H
#define FTP_H

void ftp_init();
void ftp_exit();

int ftp_frame(int s);
int ftp_getConnection();

#endif
