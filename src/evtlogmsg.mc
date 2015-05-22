;// ServerCore Service Mode Message table for NT Event Log Viewer
;//
;// Update History:
;//	2015-05-11 shkim - basic message strings

MessageIdTypeDef=DWORD

SeverityNames=
(
	Success=0x0:STATUS_SEVERITY_SUCCESS
	Informational=0x1:STATUS_SEVERITY_INFORMATIONAL
	Warning=0x2:STATUS_SEVERITY_WARNING
	Error=0x3:STATUS_SEVERITY_ERROR
)

LanguageNames=
(
	Neutral=0x0000:MSG00000
;//	English=0x401:MSG00401
;//	Korean=0x949:MSG00949
)


MessageId=1
SymbolicName=MSG_SERVICE_NOTEXIST
Severity=Error
Facility=Application
Language=English
No such named service; Couldn't set the handler.
.

MessageId=2
SymbolicName=MSG_SERVICE_INVALIDNAME
Severity=Error
Facility=Application
Language=English
Invalid service name.
.

MessageId=3
SymbolicName=MSG_SERVICE_REGISTERHANDLER_FAILED
Severity=Error
Facility=Application
Language=English
Couldn't register the Service Control Handler. (ErrorCode=%1)
.

MessageId=4
SymbolicName=MSG_REPORT_START
Severity=Informational
Facility=Application
Language=English
%1
.

MessageId=5
SymbolicName=MSG_REPORT_FINISH
Severity=Informational
Facility=Application
Language=English
ServerCore finished. If abnormal finish, please look up the Log files.
.

MessageId=6
SymbolicName=MSG_REPORT_ERROR
Severity=Error
Facility=Application
Language=English
ServerCore Error: %1
.
