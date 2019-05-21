
#include "UTSQLiteFix.h"

#include "ModuleManager.h"
#include "ModuleInterface.h"

#include "ThirdParty/sqlite/sqlite3.h"

DEFINE_LOG_CATEGORY_STATIC(UTSQL, Log, All);

#define DEBUGLOG Verbose
//#define DEBUGLOG Log

class FUTSQLiteFixPlugin : public IModuleInterface
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void ExecuteDatabaseQuery_Hook(FFrame& Stack, RESULT_DECL);
	void ServerRconDBExec_Hook(FFrame& Stack, RESULT_DECL);

	static sqlite3* Database;

	static bool ExecDatabaseCommand(const FString& DatabaseCommand, TArray<FDatabaseRow>& DatabaseRows);
};

IMPLEMENT_MODULE(FUTSQLiteFixPlugin, UTSQLiteFix)

sqlite3* FUTSQLiteFixPlugin::Database = nullptr;


//================================================
// Startup
//================================================

void FUTSQLiteFixPlugin::StartupModule()
{
	UE_LOG(LogLoad, Log, TEXT("[UTSQLiteFix] StartupModule"));

	// Open database
	FString DatabasePath = FPaths::GameSavedDir() / "Mods.db";
	if (sqlite3_open_v2(TCHAR_TO_ANSI(*DatabasePath), &Database, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr) == SQLITE_OK)
	{
		UE_LOG(LogLoad, Log, TEXT("[UTSQLiteFix] Database opened successfully"));
	}
	else
	{
		UE_LOG(LogLoad, Error, TEXT("[UTSQLiteFix] Failed to open database: %i %s"), sqlite3_extended_errcode(Database), UTF8_TO_TCHAR(sqlite3_errmsg(Database)));
		UE_LOG(LogLoad, Warning, TEXT("[UTSQLiteFix] Will fallback to stock implementation"));
		return;
	}

	// Hook UUTGameplayStatics::ExecuteDatabaseQuery
	UFunction* Func1 = UUTGameplayStatics::StaticClass()->FindFunctionByName(FName(TEXT("ExecuteDatabaseQuery")));
	Func1->FunctionFlags |= FUNC_Native;
	Func1->SetNativeFunc((Native)&FUTSQLiteFixPlugin::ExecuteDatabaseQuery_Hook);

	// Hook AUTBasePlayerController::ServerRconDBExec
	UFunction* Func2 = AUTBasePlayerController::StaticClass()->FindFunctionByName(FName(TEXT("ServerRconDBExec")));
	Func2->FunctionFlags |= FUNC_Native;
	Func2->SetNativeFunc((Native)&FUTSQLiteFixPlugin::ServerRconDBExec_Hook);
}


//================================================
// Stuff
//================================================

void FUTSQLiteFixPlugin::ExecuteDatabaseQuery_Hook(FFrame& Stack, RESULT_DECL)
{
	P_GET_OBJECT(UObject, WorldContextObject);
	PARAM_PASSED_BY_REF(DatabaseQuery, UStrProperty, FString);
	P_GET_TARRAY_REF(FDatabaseRow, OutDatebaseRows);
	P_FINISH;

	UE_LOG(UTSQL, DEBUGLOG, TEXT("ExecuteDatabaseQuery_Hook: %s"), *DatabaseQuery);

	FUTSQLiteFixPlugin::ExecDatabaseCommand(DatabaseQuery, OutDatebaseRows);
}

void FUTSQLiteFixPlugin::ServerRconDBExec_Hook(FFrame& Stack, RESULT_DECL)
{
	PARAM_PASSED_BY_REF(Command, UStrProperty, FString);
	P_FINISH;

	AUTBasePlayerController* PC = (AUTBasePlayerController*)this;

	UE_LOG(UTSQL, DEBUGLOG, TEXT("ServerRconDBExec_Hook: %s"), *Command);

	if (PC->UTPlayerState == nullptr || !PC->UTPlayerState->bIsRconAdmin)
	{
		PC->ClientSay(PC->UTPlayerState, TEXT("Rcon not authenticated"), ChatDestinations::System);
		return;
	}

	TArray<FDatabaseRow> DBRows;
	bool result = FUTSQLiteFixPlugin::ExecDatabaseCommand(Command, DBRows);
	if (DBRows.Num() > 0)
	{
		// Failsafe to not blow out on clientsay
		const int MAXROWS = 10;
		for (int i = 0; i < DBRows.Num() && i < MAXROWS; i++)
		{
			FString Message;
			for (int j = 0; j < DBRows[i].Text.Num(); j++)
			{
				Message += DBRows[i].Text[j] + TEXT(" ");
			}
			PC->ClientSay(PC->UTPlayerState, Message, ChatDestinations::System);
		}
	}
	else
	{
		PC->ClientSay(PC->UTPlayerState, result ? TEXT("DB command accepted") : TEXT("DB command rejected"), ChatDestinations::System);
	}
}

bool FUTSQLiteFixPlugin::ExecDatabaseCommand(const FString& DatabaseCommand, TArray<FDatabaseRow>& DatabaseRows)
{
	if (!Database)	// can't happen
	{
		return false;
	}

	sqlite3_stmt* DatabaseStatement = nullptr;

	if (sqlite3_prepare_v2(Database, TCHAR_TO_UTF8(*DatabaseCommand), -1, &DatabaseStatement, nullptr) != SQLITE_OK)
	{
		UE_LOG(UTSQL, Warning, TEXT("SQL statement failed:\n%s\n%i %s"), *DatabaseCommand, sqlite3_extended_errcode(Database), UTF8_TO_TCHAR(sqlite3_errmsg(Database)));
		return false;
	}

	int step = sqlite3_step(DatabaseStatement);
	while (step == SQLITE_ROW)
	{
		FDatabaseRow NewRow;
		int DBColumnCount = sqlite3_column_count(DatabaseStatement);
		for (int i = 0; i < DBColumnCount; i++)
		{
			int DBColumnType = sqlite3_column_type(DatabaseStatement, i);
			if (DBColumnType == SQLITE_TEXT)
			{
				const unsigned char* DBText = sqlite3_column_text(DatabaseStatement, i);
				NewRow.Text.Add(UTF8_TO_TCHAR((char*)DBText));
			}
			else if (DBColumnType == SQLITE_INTEGER)
			{
				int DBInteger = sqlite3_column_int(DatabaseStatement, i);
				NewRow.Text.Add(FString::Printf(TEXT("%d"), DBInteger));
			}
			else if (DBColumnType == SQLITE_FLOAT)
			{
				float DBFloat = sqlite3_column_double(DatabaseStatement, i);
				NewRow.Text.Add(FString::Printf(TEXT("%f"), DBFloat));
			}
		}
		DatabaseRows.Add(NewRow);
		step = sqlite3_step(DatabaseStatement);
	}

	if (step != SQLITE_OK && step != SQLITE_DONE)
	{
		UE_LOG(UTSQL, Warning, TEXT("SQL step failed:\n%s\n%i %s"), *DatabaseCommand, sqlite3_extended_errcode(Database), UTF8_TO_TCHAR(sqlite3_errmsg(Database)));
		sqlite3_finalize(DatabaseStatement);
		return false;
	}

	if (sqlite3_finalize(DatabaseStatement) != SQLITE_OK)
	{
		UE_LOG(UTSQL, Warning, TEXT("SQL finalize failed:\n%s\n%i %s"), *DatabaseCommand, sqlite3_extended_errcode(Database), UTF8_TO_TCHAR(sqlite3_errmsg(Database)));
		return false;
	}

	return true;
}


//================================================
// Shutdown
//================================================

void FUTSQLiteFixPlugin::ShutdownModule()
{
	UE_LOG(LogLoad, Log, TEXT("[UTSQLiteFix] ShutdownModule"));

	if (Database)
	{
		if (sqlite3_close(Database) == SQLITE_OK)
		{
			UE_LOG(LogLoad, Log, TEXT("[UTSQLiteFix] Database closed successfully"));
		}
		else
		{
			UE_LOG(LogLoad, Warning, TEXT("[UTSQLiteFix] Failed to close database: %i %s"), sqlite3_extended_errcode(Database), UTF8_TO_TCHAR(sqlite3_errmsg(Database)));
		}
	}
}
