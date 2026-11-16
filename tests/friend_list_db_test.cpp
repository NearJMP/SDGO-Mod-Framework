#include <windows.h>
#include <msdasc.h>
#include <cstdio>
#include "../mods/friend_list_fix/policy.h"
using namespace sdgo::friend_fix;
static std::wstring Read(const wchar_t* path,const wchar_t* key,const wchar_t* section=L"fixture") {
    wchar_t text[2048]{};const auto n=GetPrivateProfileStringW(section,key,L"",text,2048,path);
    if(!n||n>=2047)throw std::runtime_error("fixture setting missing");return text;
}
static std::wstring Quote(const std::wstring& s) {
    std::wstring out=L"\"";for(auto c:s){out+=c;if(c==L'"')out+=c;}return out+L"\"";
}
// Exercise the production metadata query using session-local temporary tables.
// Only its catalog/table address changes; the type/width/collation checks do not.
static void CheckPilotNameLayouts(Session& db) {
    std::wstring query=kPilotNameCollationQuery;
    const auto retarget=[&](const std::wstring& from,const std::wstring& to) {
        const auto at=query.find(from);Need(at!=std::wstring::npos,"metadata query target missing");
        query.replace(at,from.size(),to);
    };
    retarget(L"FROM sys.columns",L"FROM tempdb.sys.columns");
    retarget(L"dbo.tbl_Account_Pilot",L"tempdb..#sdgo_friend_name_layout");
    struct Case { const wchar_t* column;const wchar_t* expected;const char* label; };
    const Case cases[]={
        {L"f_pilot nvarchar(20) COLLATE Chinese_PRC_CS_AI",L"Chinese_PRC_CS_AI","legacy nvarchar(20)"},
        {L"f_pilot nvarchar(50) COLLATE Thai_CI_AS",L"Thai_CI_AS","wider nvarchar(50)"},
        {L"f_pilot nvarchar(4000) COLLATE Chinese_PRC_CS_AI",L"Chinese_PRC_CS_AI","bounded maximum nvarchar(4000)"},
        {L"f_pilot nvarchar(19)",L"","undersized Unicode"},
        {L"f_pilot varchar(50)",L"","non-Unicode"},
        {L"f_pilot nchar(50)",L"","fixed-width Unicode"},
        {L"f_pilot nvarchar(max)",L"","unbounded Unicode"},
        {L"f_pilot int",L"","non-string"},
        {L"other_name nvarchar(50)",L"","missing pilot column"},
    };
    for(const auto& item:cases) {
        Need(db.Exec(L"CREATE TABLE #sdgo_friend_name_layout("+std::wstring(item.column)+L")"),"temporary metadata table creation failed");
        std::wstring actual;
        const bool read=db.String(query,actual);
        Need(db.Exec(L"DROP TABLE #sdgo_friend_name_layout"),"temporary metadata table cleanup failed");
        Need(read,"pilot metadata query failed");
        if(actual!=item.expected){fprintf(stderr,"pilot metadata case failed: %s\n",item.label);throw std::runtime_error("pilot metadata compatibility mismatch");}
        printf("PASS pilot metadata: %s\n",item.label);
    }
}
int wmain(int argc,wchar_t** argv) {
    const bool nameLayoutOnly=argc==3&&!wcscmp(argv[1],L"--name-layout-only");
    if((argc!=2&&!nameLayoutOnly)||FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
    try {
        Com<IDataInitialize> init;Com<IDBInitialize> source;Com<IDBCreateSession> maker;Com<IDBCreateCommand> factory;
        // The metadata-only mode reads an existing native INI and performs no
        // policy initialization, persistent DDL or player-data queries.
        const auto path=argv[nameLayoutOnly?2:1];
        const auto section=nameLayoutOnly?L"DB Member":L"fixture";
        const auto database=Read(path,nameLayoutOnly?L"DBName":L"database",section);
        const auto user=nameLayoutOnly?Read(path,L"id",section):L"sa";
        const auto connection=L"Provider=SQLOLEDB;OLE DB Services=-2;Data Source="+Quote(Read(path,nameLayoutOnly?L"DBServerName":L"server",section))+
            L";Initial Catalog="+Quote(database)+L";User ID="+Quote(user)+L";Password="+Quote(Read(path,nameLayoutOnly?L"pwd":L"password",section))+L";";
        Need(SUCCEEDED(CoCreateInstance(CLSID_MSDAINITIALIZE,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IDataInitialize),reinterpret_cast<void**>(init.put()))),"fixture IDataInitialize unavailable");
        const auto sourceResult=init->GetDataSource(nullptr,CLSCTX_INPROC_SERVER,connection.c_str(),__uuidof(IDBInitialize),reinterpret_cast<IUnknown**>(source.put()));
        if(FAILED(sourceResult)){fprintf(stderr,"fixture provider HRESULT=%08lX\n",static_cast<unsigned long>(sourceResult));throw std::runtime_error("fixture data source creation failed");}
        Need(SUCCEEDED(source->Initialize()),"fixture data source authentication failed");
        Need(SUCCEEDED(source->QueryInterface(__uuidof(IDBCreateSession),reinterpret_cast<void**>(maker.put()))),"fixture session factory unavailable");
        Need(SUCCEEDED(maker->CreateSession(nullptr,__uuidof(IDBCreateCommand),reinterpret_cast<IUnknown**>(factory.put()))),"fixture command factory creation failed");
        Com<ICommandText> native;Need(SUCCEEDED(factory->CreateCommand(nullptr,__uuidof(ICommandText),reinterpret_cast<IUnknown**>(native.put()))),"fixture native command creation failed");
        struct Connection { BYTE pad[8]{};ICommandText* command;BYTE rest[0x240-12]{};int state=0; } lease{};
        static_assert(offsetof(Connection,command)==8&&offsetof(Connection,state)==0x240);
        lease.command=native.get();Session db;Need(db.Open(&lease),"fixture borrowed command session unavailable");
        puts("fixture: borrowed session open");fflush(stdout);
        CheckPilotNameLayouts(db);
        if(nameLayoutOnly) {
            std::wstring collation,definition;
            Need(db.String(kPilotNameCollationQuery,collation)&&!collation.empty(),"selected native pilot column incompatible");
            Need(db.String(L"SELECT OBJECT_DEFINITION(OBJECT_ID(N'dbo.sp_FriendsList'))",definition),"native friend definition unavailable");
            Need(!Clone(definition,L"sp_FriendsList",false,collation).empty());
            Need(!Clone(definition,L"sp_FriendsList",false,collation,Read(path,L"DBName",L"DB Game")).empty());
            puts("PASS selected native pilot metadata and both owned query variants (read-only)");
            return 0;
        }
        std::wstring before,after;
        Need(db.String(L"SELECT OBJECT_DEFINITION(OBJECT_ID(N'dbo.sp_FriendsList'))",before));
        puts("fixture: native definition read");fflush(stdout);
        Need(!Prepare(db,L"wrong_database"));
        Need(db.Begin());bool refused=false;
        try{Prepare(db,database);}catch(...){refused=true;}
        Need(refused,"joined caller transaction");db.Abort();
        puts("fixture: caller transaction refusal passed");fflush(stdout);
        Need(Prepare(db,database));Need(Prepare(db,database));
        puts("fixture: policy initialization passed twice");fflush(stdout);
        Need(db.String(L"SELECT OBJECT_DEFINITION(OBJECT_ID(N'dbo.sp_FriendsList'))",after));
        Need(before==after,"native SQL definition changed");
        puts("fixture: native definition preserved");fflush(stdout);
        Need(db.Begin());
        Need(db.Exec(L"ALTER PROCEDURE [sdgo_friend_list_fix].[sp_FriendsList] AS SELECT 1"));
        // Commit is deliberate inside this disposable database: initialization
        // must detect an external edit on the next authenticated session.
        Need(db.Commit());refused=false;
        try{Prepare(db,database);}catch(...){refused=true;}
        Need(refused,"external clone edit was overwritten");
        // Restore the fixture's owned procedure and its original creation options.
        std::wstring flags;
        Need(db.String(L"SELECT CONVERT(nvarchar(10),CONVERT(int,uses_ansi_nulls)+2*CONVERT(int,uses_quoted_identifier)) FROM sys.sql_modules WHERE object_id=OBJECT_ID(N'dbo.sp_FriendsList')",flags));
        const int options=std::stoi(flags);
        Need(db.Exec(std::wstring(L"SET ANSI_NULLS ")+(options&1?L"ON":L"OFF")));
        Need(db.Exec(std::wstring(L"SET QUOTED_IDENTIFIER ")+(options&2?L"ON":L"OFF")));
        std::wstring collation;
        Need(db.String(L"SELECT collation_name FROM sys.columns WHERE object_id=OBJECT_ID(N'dbo.tbl_Account_Pilot') AND name=N'f_pilot'",collation));
        Need(db.Exec(Clone(before,L"sp_FriendsList",true,collation)));
        Need(Prepare(db,database));
        puts("PASS actual SQLOLEDB borrowed session, ownership, retry, source preservation and external-edit refusal");
        return 0;
    }catch(const std::exception& e){fprintf(stderr,"friend DB fixture: %s\n",e.what());return 1;}
}
