// ServerCore Service Mode Message table for NT Event Log Viewer
//
//	English=0x401:MSG00401
//	Korean=0x949:MSG00949
//
//  Values are 32 bit values layed out as follows:
//
//   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
//   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
//  +---+-+-+-----------------------+-------------------------------+
//  |Sev|C|R|     Facility          |               Code            |
//  +---+-+-+-----------------------+-------------------------------+
//
//  where
//
//      Sev - is the severity code
//
//          00 - Success
//          01 - Informational
//          10 - Warning
//          11 - Error
//
//      C - is the Customer code flag
//
//      R - is a reserved bit
//
//      Facility - is the facility code
//
//      Code - is the facility's status code
//
//
// Define the facility codes
//


//
// Define the severity codes
//
#define STATUS_SEVERITY_WARNING          0x2
#define STATUS_SEVERITY_SUCCESS          0x0
#define STATUS_SEVERITY_INFORMATIONAL    0x1
#define STATUS_SEVERITY_ERROR            0x3


//
// MessageId: MSG_SERVICE_NOTEXIST
//
// MessageText:
//
//  No such named service; Couldn't set the handler.
//
#define MSG_SERVICE_NOTEXIST             ((DWORD)0xE0000001L)

//
// MessageId: MSG_SERVICE_INVALIDNAME
//
// MessageText:
//
//  Invalid service name.
//
#define MSG_SERVICE_INVALIDNAME          ((DWORD)0xE0000002L)

//
// MessageId: MSG_SERVICE_REGISTERHANDLER_FAILED
//
// MessageText:
//
//  Couldn't register the Service Control Handler. (ErrorCode=%1)
//
#define MSG_SERVICE_REGISTERHANDLER_FAILED ((DWORD)0xE0000003L)

//
// MessageId: MSG_REPORT_START
//
// MessageText:
//
//  %1
//
#define MSG_REPORT_START                 ((DWORD)0x60000004L)

//
// MessageId: MSG_REPORT_FINISH
//
// MessageText:
//
//  ServerCore finished. If abnormal finish, please look up the Log files.
//
#define MSG_REPORT_FINISH                ((DWORD)0x60000005L)

//
// MessageId: MSG_REPORT_ERROR
//
// MessageText:
//
//  ServerCore Error: %1
//
#define MSG_REPORT_ERROR                 ((DWORD)0xE0000006L)

