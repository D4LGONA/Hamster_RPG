// 2026-11-12: Database.h 참고.

#include "stdafx.h"
#include "Database.h"

#include <sql.h>
#include <sqlext.h>
#include <fstream>

#ifdef _WIN32
#pragma comment(lib, "odbc32.lib")
// 프로젝트가 유니코드 설정이라 SQLDriverConnect는 W 버전으로 바뀐다.
// 연결 문자열/SQL 문장은 영어뿐이라 A 버전을 명시해서 쓰고, 이름(한글)만 UTF-16으로 넘긴다.
#define ODBC_DRIVER_CONNECT SQLDriverConnectA
#define ODBC_PREPARE        SQLPrepareA
#define ODBC_GET_DIAG_REC   SQLGetDiagRecA
#else
// 리눅스(unixODBC)에서 같은 코드를 시험할 때. A 접미사가 없는 쪽이 ANSI다.
#define ODBC_DRIVER_CONNECT SQLDriverConnect
#define ODBC_PREPARE        SQLPrepare
#define ODBC_GET_DIAG_REC   SQLGetDiagRec
#endif

namespace
{
    // 클라가 보내는 이름은 UTF-8이다. ODBC의 ANSI(A) 함수로 넘기면 윈도우가 CP949로 오해해서 한글이 깨진다.
    // 그래서 UTF-16으로 바꿔 SQL_C_WCHAR로 넘긴다. (SQLWCHAR는 윈도우/unixODBC 모두 2바이트)
    std::u16string Utf8ToUtf16(const std::string& text)
    {
        std::u16string out;
        std::size_t i = 0;

        while (i < text.size())
        {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            std::uint32_t cp = 0;
            std::size_t extra = 0;

            if (c < 0x80) { cp = c; extra = 0; }
            else if ((c >> 5) == 0x6) { cp = c & 0x1F; extra = 1; }
            else if ((c >> 4) == 0xE) { cp = c & 0x0F; extra = 2; }
            else if ((c >> 3) == 0x1E) { cp = c & 0x07; extra = 3; }
            else { ++i; continue; }   // 깨진 바이트는 건너뛴다

            if (i + extra >= text.size())
            {
                break;   // 문자열 끝에서 잘린 글자
            }

            for (std::size_t k = 1; k <= extra; ++k)
            {
                cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
            }

            i += extra + 1;

            if (cp >= 0x10000)
            {
                cp -= 0x10000;
                out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
                out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
            }
            else
            {
                out.push_back(static_cast<char16_t>(cp));
            }
        }

        return out;
    }

    std::string Utf16ToUtf8(const char16_t* text, std::size_t length)
    {
        std::string out;

        for (std::size_t i = 0; i < length; ++i)
        {
            std::uint32_t cp = text[i];

            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < length)
            {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (text[i + 1] - 0xDC00);
                ++i;
            }

            if (cp < 0x80) { out.push_back(static_cast<char>(cp)); }
            else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
            else if (cp < 0x10000) { out.push_back(static_cast<char>(0xE0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
            else { out.push_back(static_cast<char>(0xF0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        }

        return out;
    }

    // 프로시저 CALL은 결과 집합 뒤에 상태 결과가 하나 더 온다. 다 비워야 다음 문장을 실행할 수 있다.
    void DrainResults(SQLHSTMT stmt)
    {
        while (SQLMoreResults(stmt) == SQL_SUCCESS)
        {
        }
        SQLFreeStmt(stmt, SQL_CLOSE);
    }

    bool Ok(SQLRETURN r) { return r == SQL_SUCCESS || r == SQL_SUCCESS_WITH_INFO; }
}

Database::~Database()
{
    Disconnect();
}

bool Database::Connect(const std::string& newConnectionString)
{
    Disconnect();
    connectionString = newConnectionString;

    SQLHENV newEnv = SQL_NULL_HENV;
    SQLHDBC newDbc = SQL_NULL_HDBC;

    if (Ok(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &newEnv)) == false)
    {
        lastError = "SQLAllocHandle(ENV) failed";
        return false;
    }

    SQLSetEnvAttr(newEnv, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);

    if (Ok(SQLAllocHandle(SQL_HANDLE_DBC, newEnv, &newDbc)) == false)
    {
        lastError = "SQLAllocHandle(DBC) failed";
        SQLFreeHandle(SQL_HANDLE_ENV, newEnv);
        return false;
    }

    SQLSetConnectAttr(newDbc, SQL_LOGIN_TIMEOUT, reinterpret_cast<SQLPOINTER>(static_cast<SQLLEN>(3)), 0);

    SQLRETURN r = ODBC_DRIVER_CONNECT(newDbc, nullptr,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>(connectionString.c_str())), SQL_NTS,
        nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT);

    if (Ok(r) == false)
    {
        RecordError(SQL_HANDLE_DBC, newDbc, "connect");
        SQLFreeHandle(SQL_HANDLE_DBC, newDbc);
        SQLFreeHandle(SQL_HANDLE_ENV, newEnv);
        return false;
    }

    env = newEnv;
    dbc = newDbc;
    return true;
}

void Database::Disconnect()
{
    if (dbc != nullptr)
    {
        SQLDisconnect(static_cast<SQLHDBC>(dbc));
        SQLFreeHandle(SQL_HANDLE_DBC, static_cast<SQLHDBC>(dbc));
        dbc = nullptr;
    }

    if (env != nullptr)
    {
        SQLFreeHandle(SQL_HANDLE_ENV, static_cast<SQLHENV>(env));
        env = nullptr;
    }
}

// MySQL은 8시간 놀면 연결을 끊는다(wait_timeout). 실패하면 한 번 다시 붙어서 재시도한다.
bool Database::ReconnectIfDead()
{
    if (dbc != nullptr)
    {
        SQLUINTEGER dead = SQL_CD_FALSE;
        SQLGetConnectAttr(static_cast<SQLHDBC>(dbc), SQL_ATTR_CONNECTION_DEAD, &dead, 0, nullptr);

        if (dead == SQL_CD_FALSE)
        {
            return false;   // 살아 있는데 실패했다 = 쿼리 문제. 다시 붙어도 소용없다
        }
    }

    std::cout << "[DB] connection lost. reconnecting..." << std::endl;
    std::string saved = connectionString;
    return Connect(saved);
}

bool Database::LoadCharacter(const std::string& name, CharacterData& out)
{
    if (dbc == nullptr)
    {
        return false;
    }

    if (TryLoad(name, out))
    {
        return true;
    }

    return ReconnectIfDead() && TryLoad(name, out);
}

bool Database::SaveCharacters(const std::vector<CharacterData>& list)
{
    if (dbc == nullptr || list.empty())
    {
        return list.empty();
    }

    if (TrySave(list))
    {
        return true;
    }

    return ReconnectIfDead() && TrySave(list);
}

bool Database::TryLoad(const std::string& name, CharacterData& out)
{
    SQLHDBC conn = static_cast<SQLHDBC>(dbc);
    SQLHSTMT stmt = SQL_NULL_HSTMT;

    if (Ok(SQLAllocHandle(SQL_HANDLE_STMT, conn, &stmt)) == false)
    {
        RecordError(SQL_HANDLE_DBC, conn, "load alloc");
        return false;
    }

    std::u16string wideName = Utf8ToUtf16(name);
    SQLLEN nameBytes = static_cast<SQLLEN>(wideName.size() * sizeof(char16_t));

    bool success = false;

    if (Ok(ODBC_PREPARE(stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>("{CALL sp_character_load(?)}")), SQL_NTS)) &&
        Ok(SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 20, 0,
                            const_cast<char16_t*>(wideName.c_str()), nameBytes, &nameBytes)) &&
        Ok(SQLExecute(stmt)))
    {
        // 프로시저 안의 INSERT/UPDATE가 행 수 결과를 먼저 낼 수 있어서, 열이 있는 결과 집합까지 넘긴다.
        SQLSMALLINT columns = 0;
        SQLNumResultCols(stmt, &columns);

        while (columns == 0 && Ok(SQLMoreResults(stmt)))
        {
            SQLNumResultCols(stmt, &columns);
        }

        if (columns >= 8 && Ok(SQLFetch(stmt)))
        {
            SQLINTEGER level = 1, exp = 0, gold = 0, hp = 100, isNew = 0, weaponSlot = -1;
            SQLREAL x = 0.0f, y = 0.0f;
            SQLLEN ind = 0;

            SQLGetData(stmt, 1, SQL_C_SLONG, &level, 0, &ind);
            SQLGetData(stmt, 2, SQL_C_SLONG, &exp, 0, &ind);
            SQLGetData(stmt, 3, SQL_C_SLONG, &gold, 0, &ind);
            SQLGetData(stmt, 4, SQL_C_SLONG, &hp, 0, &ind);
            SQLGetData(stmt, 5, SQL_C_FLOAT, &x, 0, &ind);
            SQLGetData(stmt, 6, SQL_C_FLOAT, &y, 0, &ind);
            SQLGetData(stmt, 7, SQL_C_SLONG, &isNew, 0, &ind);
            SQLGetData(stmt, 8, SQL_C_SLONG, &weaponSlot, 0, &ind);

            out.name = name;
            out.level = level;
            out.exp = exp;
            out.gold = gold;
            out.hp = hp;
            out.x = x;
            out.y = y;
            out.isNew = isNew != 0;
            out.equippedSlot = weaponSlot;
            out.items.clear();
            success = true;

            // 두 번째 결과 집합: 인벤토리 (slot, item_id, count). SQLMoreResults가 남은 줄을 버리고 다음 집합으로 넘어간다
            // (SQLCloseCursor를 부르면 남은 결과 집합까지 다 버려지니 쓰지 않는다)
            if (Ok(SQLMoreResults(stmt)))
            {
                SQLNumResultCols(stmt, &columns);

                while (columns == 0 && Ok(SQLMoreResults(stmt)))
                {
                    SQLNumResultCols(stmt, &columns);
                }

                while (columns >= 4 && Ok(SQLFetch(stmt)))
                {
                    SQLINTEGER slot = 0, itemId = 0, count = 0, enhance = 0;
                    SQLGetData(stmt, 1, SQL_C_SLONG, &slot, 0, &ind);
                    SQLGetData(stmt, 2, SQL_C_SLONG, &itemId, 0, &ind);
                    SQLGetData(stmt, 3, SQL_C_SLONG, &count, 0, &ind);
                    SQLGetData(stmt, 4, SQL_C_SLONG, &enhance, 0, &ind);
                    out.items.push_back({ slot, static_cast<std::uint16_t>(itemId), static_cast<std::uint16_t>(count), enhance });
                }
            }
        }
    }

    if (success == false)
    {
        RecordError(SQL_HANDLE_STMT, stmt, "load");
    }

    DrainResults(stmt);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return success;
}

bool Database::TrySave(const std::vector<CharacterData>& list)
{
    SQLHDBC conn = static_cast<SQLHDBC>(dbc);
    SQLHSTMT stmt = SQL_NULL_HSTMT;

    if (Ok(SQLAllocHandle(SQL_HANDLE_STMT, conn, &stmt)) == false)
    {
        RecordError(SQL_HANDLE_DBC, conn, "save alloc");
        return false;
    }

    // 한 번 준비한 문장에 값만 바꿔 끼워 반복 실행한다. 바인딩은 아래 변수들의 주소를 가리킨다.
    std::u16string wideName;
    SQLLEN nameBytes = 0;
    SQLINTEGER level = 0, exp = 0, gold = 0, hp = 0, weaponSlot = -1;
    SQLREAL x = 0.0f, y = 0.0f;
    char16_t nameBuffer[32] = {};

    // 인벤토리 한 칸 저장용 두 번째 문장. 이름 버퍼는 위와 같은 것을 같이 가리킨다.
    SQLHSTMT itemStmt = SQL_NULL_HSTMT;
    SQLINTEGER itemSlot = 0, itemId = 0, itemCount = 0, itemEnhance = 0;

    if (Ok(SQLAllocHandle(SQL_HANDLE_STMT, conn, &itemStmt)) == false)
    {
        RecordError(SQL_HANDLE_DBC, conn, "save alloc item");
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return false;
    }

    bool success =
        Ok(ODBC_PREPARE(stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>("{CALL sp_character_save(?, ?, ?, ?, ?, ?, ?, ?)}")), SQL_NTS)) &&
        Ok(SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 20, 0, nameBuffer, sizeof(nameBuffer), &nameBytes)) &&
        Ok(SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &level, 0, nullptr)) &&
        Ok(SQLBindParameter(stmt, 3, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &exp, 0, nullptr)) &&
        Ok(SQLBindParameter(stmt, 4, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &gold, 0, nullptr)) &&
        Ok(SQLBindParameter(stmt, 5, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &hp, 0, nullptr)) &&
        Ok(SQLBindParameter(stmt, 6, SQL_PARAM_INPUT, SQL_C_FLOAT, SQL_REAL, 0, 0, &x, 0, nullptr)) &&
        Ok(SQLBindParameter(stmt, 7, SQL_PARAM_INPUT, SQL_C_FLOAT, SQL_REAL, 0, 0, &y, 0, nullptr)) &&
        Ok(SQLBindParameter(stmt, 8, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &weaponSlot, 0, nullptr)) &&
        Ok(ODBC_PREPARE(itemStmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>("{CALL sp_inventory_set(?, ?, ?, ?, ?)}")), SQL_NTS)) &&
        Ok(SQLBindParameter(itemStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 20, 0, nameBuffer, sizeof(nameBuffer), &nameBytes)) &&
        Ok(SQLBindParameter(itemStmt, 2, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &itemSlot, 0, nullptr)) &&
        Ok(SQLBindParameter(itemStmt, 3, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &itemId, 0, nullptr)) &&
        Ok(SQLBindParameter(itemStmt, 4, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &itemCount, 0, nullptr)) &&
        Ok(SQLBindParameter(itemStmt, 5, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &itemEnhance, 0, nullptr));

    if (success)
    {
        // 전부 저장되거나 전부 안 되거나. 중간에 끊기면 일부만 저장된 어정쩡한 상태가 남지 않게.
        SQLSetConnectAttr(conn, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_OFF), 0);

        for (const CharacterData& c : list)
        {
            wideName = Utf8ToUtf16(c.name);
            std::size_t count = wideName.size() < 31 ? wideName.size() : 31;
            std::copy(wideName.begin(), wideName.begin() + count, nameBuffer);
            nameBuffer[count] = 0;
            nameBytes = static_cast<SQLLEN>(count * sizeof(char16_t));

            level = c.level; exp = c.exp; gold = c.gold; hp = c.hp; x = c.x; y = c.y; weaponSlot = c.equippedSlot;

            if (Ok(SQLExecute(stmt)) == false)
            {
                success = false;
                break;
            }

            DrainResults(stmt);

            // 가방: sp_character_save가 비웠으니 남은 칸만 다시 넣는다
            for (const CharacterData::ItemRow& row : c.items)
            {
                itemSlot = row.slot; itemId = row.itemId; itemCount = row.count; itemEnhance = row.enhance;

                if (Ok(SQLExecute(itemStmt)) == false)
                {
                    RecordError(SQL_HANDLE_STMT, itemStmt, "save item");
                    success = false;
                    break;
                }

                DrainResults(itemStmt);
            }

            if (success == false)
            {
                break;
            }
        }

        if (success == false && lastError.rfind("save item", 0) != 0)
        {
            RecordError(SQL_HANDLE_STMT, stmt, "save");
        }

        SQLEndTran(SQL_HANDLE_DBC, conn, success ? SQL_COMMIT : SQL_ROLLBACK);
        SQLSetConnectAttr(conn, SQL_ATTR_AUTOCOMMIT, reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), 0);
    }
    else
    {
        RecordError(SQL_HANDLE_STMT, stmt, "save prepare");
    }

    SQLFreeHandle(SQL_HANDLE_STMT, itemStmt);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return success;
}

void Database::RecordError(short handleType, void* handle, const char* where)
{
    SQLCHAR state[6] = {};
    SQLCHAR message[512] = {};
    SQLINTEGER nativeError = 0;
    SQLSMALLINT length = 0;

    lastError = where;

    if (handle != nullptr &&
        Ok(ODBC_GET_DIAG_REC(handleType, handle, 1, state, &nativeError, message, sizeof(message), &length)))
    {
        lastError += std::string(" [") + reinterpret_cast<char*>(state) + "] " + reinterpret_cast<char*>(message);
    }
}

// ---------------------------------------------------------------- 집 / 가구 (2026-11-13)

// 결과 없는 프로시저 하나 실행. bind가 파라미터를 묶는다. 실패하면 한 번 다시 붙어서 재시도.
bool Database::ExecCall(const char* sql, const std::function<bool(void* stmt)>& bind)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        if (dbc == nullptr)
        {
            return false;
        }

        SQLHSTMT stmt = SQL_NULL_HSTMT;

        if (Ok(SQLAllocHandle(SQL_HANDLE_STMT, static_cast<SQLHDBC>(dbc), &stmt)) == false)
        {
            RecordError(SQL_HANDLE_DBC, dbc, "exec alloc");
            return false;
        }

        bool ok = Ok(ODBC_PREPARE(stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql)), SQL_NTS)) &&
                  bind(stmt) &&
                  Ok(SQLExecute(stmt));

        if (ok == false)
        {
            RecordError(SQL_HANDLE_STMT, stmt, sql);
        }

        DrainResults(stmt);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);

        if (ok)
        {
            return true;
        }

        if (attempt == 0 && ReconnectIfDead() == false)
        {
            return false;
        }
    }

    return false;
}

bool Database::CreateHouse(const DbHouseRow& row)
{
    std::u16string wideName = Utf8ToUtf16(row.ownerName);
    SQLLEN nameBytes = static_cast<SQLLEN>(wideName.size() * sizeof(char16_t));
    SQLBIGINT id = static_cast<SQLBIGINT>(row.houseId);
    SQLREAL x = row.x, y = row.y;

    return ExecCall("{CALL sp_house_create(?, ?, ?, ?)}", [&](void* h)
    {
        SQLHSTMT stmt = static_cast<SQLHSTMT>(h);
        return Ok(SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, &id, 0, nullptr)) &&
               Ok(SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 20, 0, const_cast<char16_t*>(wideName.c_str()), nameBytes, &nameBytes)) &&
               Ok(SQLBindParameter(stmt, 3, SQL_PARAM_INPUT, SQL_C_FLOAT, SQL_REAL, 0, 0, &x, 0, nullptr)) &&
               Ok(SQLBindParameter(stmt, 4, SQL_PARAM_INPUT, SQL_C_FLOAT, SQL_REAL, 0, 0, &y, 0, nullptr));
    });
}

bool Database::SaveFurniture(const DbFurnitureRow& row)
{
    SQLBIGINT id = static_cast<SQLBIGINT>(row.furnitureId);
    SQLBIGINT houseId = static_cast<SQLBIGINT>(row.houseId);
    SQLINTEGER itemType = row.itemType, rotation = row.rotation;
    SQLREAL x = row.x, y = row.y;

    return ExecCall("{CALL sp_furniture_save(?, ?, ?, ?, ?, ?)}", [&](void* h)
    {
        SQLHSTMT stmt = static_cast<SQLHSTMT>(h);
        return Ok(SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, &id, 0, nullptr)) &&
               Ok(SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, &houseId, 0, nullptr)) &&
               Ok(SQLBindParameter(stmt, 3, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &itemType, 0, nullptr)) &&
               Ok(SQLBindParameter(stmt, 4, SQL_PARAM_INPUT, SQL_C_FLOAT, SQL_REAL, 0, 0, &x, 0, nullptr)) &&
               Ok(SQLBindParameter(stmt, 5, SQL_PARAM_INPUT, SQL_C_FLOAT, SQL_REAL, 0, 0, &y, 0, nullptr)) &&
               Ok(SQLBindParameter(stmt, 6, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &rotation, 0, nullptr));
    });
}

bool Database::DeleteFurniture(std::uint64_t furnitureId)
{
    SQLBIGINT id = static_cast<SQLBIGINT>(furnitureId);

    return ExecCall("{CALL sp_furniture_delete(?)}", [&](void* h)
    {
        return Ok(SQLBindParameter(static_cast<SQLHSTMT>(h), 1, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, &id, 0, nullptr));
    });
}

bool Database::LoadWorld(std::vector<DbHouseRow>& housesOut, std::vector<DbFurnitureRow>& furnitureOut)
{
    if (dbc == nullptr)
    {
        return false;
    }

    if (TryLoadWorld(housesOut, furnitureOut))
    {
        return true;
    }

    housesOut.clear();
    furnitureOut.clear();
    return ReconnectIfDead() && TryLoadWorld(housesOut, furnitureOut);
}

// sp_world_load: 결과 1 = 집 전부, 결과 2 = 가구 전부
bool Database::TryLoadWorld(std::vector<DbHouseRow>& housesOut, std::vector<DbFurnitureRow>& furnitureOut)
{
    SQLHSTMT stmt = SQL_NULL_HSTMT;

    if (Ok(SQLAllocHandle(SQL_HANDLE_STMT, static_cast<SQLHDBC>(dbc), &stmt)) == false)
    {
        RecordError(SQL_HANDLE_DBC, dbc, "world alloc");
        return false;
    }

    bool ok = Ok(ODBC_PREPARE(stmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>("{CALL sp_world_load()}")), SQL_NTS)) &&
              Ok(SQLExecute(stmt));

    if (ok)
    {
        SQLSMALLINT columns = 0;
        SQLLEN ind = 0;
        SQLNumResultCols(stmt, &columns);

        // 집: house_id, owner_name, world_x, world_y
        while (columns >= 4 && Ok(SQLFetch(stmt)))
        {
            SQLBIGINT id = 0;
            char16_t name[32] = {};
            SQLLEN nameLen = 0;
            SQLREAL x = 0.0f, y = 0.0f;

            SQLGetData(stmt, 1, SQL_C_SBIGINT, &id, 0, &ind);
            SQLGetData(stmt, 2, SQL_C_WCHAR, name, sizeof(name), &nameLen);
            SQLGetData(stmt, 3, SQL_C_FLOAT, &x, 0, &ind);
            SQLGetData(stmt, 4, SQL_C_FLOAT, &y, 0, &ind);

            std::size_t chars = nameLen > 0 ? static_cast<std::size_t>(nameLen) / sizeof(char16_t) : 0;
            if (chars > 31) { chars = 31; }
            housesOut.push_back({ static_cast<std::uint64_t>(id), Utf16ToUtf8(name, chars), x, y });
        }

        // 가구: furniture_id, house_id, item_type, x, y, rotation
        if (Ok(SQLMoreResults(stmt)))
        {
            SQLNumResultCols(stmt, &columns);

            while (columns >= 6 && Ok(SQLFetch(stmt)))
            {
                SQLBIGINT id = 0, houseId = 0;
                SQLINTEGER itemType = 0, rotation = 0;
                SQLREAL x = 0.0f, y = 0.0f;

                SQLGetData(stmt, 1, SQL_C_SBIGINT, &id, 0, &ind);
                SQLGetData(stmt, 2, SQL_C_SBIGINT, &houseId, 0, &ind);
                SQLGetData(stmt, 3, SQL_C_SLONG, &itemType, 0, &ind);
                SQLGetData(stmt, 4, SQL_C_FLOAT, &x, 0, &ind);
                SQLGetData(stmt, 5, SQL_C_FLOAT, &y, 0, &ind);
                SQLGetData(stmt, 6, SQL_C_SLONG, &rotation, 0, &ind);

                furnitureOut.push_back({ static_cast<std::uint64_t>(id), static_cast<std::uint64_t>(houseId), itemType, x, y, rotation });
            }
        }
    }
    else
    {
        RecordError(SQL_HANDLE_STMT, stmt, "world load");
    }

    DrainResults(stmt);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return ok;
}

// ---------------------------------------------------------------- DbWorker

bool DbWorker::Start(const std::string& connectionString)
{
    if (connectionString.empty())
    {
        std::cout << "[DB] db_config.txt not found. running WITHOUT database (characters are not saved)." << std::endl;
        return false;
    }

    if (db.Connect(connectionString) == false)
    {
        std::cout << "[DB] connect failed: " << db.GetLastError() << std::endl;
        std::cout << "[DB] running WITHOUT database (characters are not saved)." << std::endl;
        return false;
    }

    enabled = true;
    thread = std::thread([this]() { Run(); });
    std::cout << "[DB] connected." << std::endl;
    return true;
}

void DbWorker::Stop()
{
    {
        std::lock_guard<std::mutex> lock(queueLock);
        stopping = true;
    }
    queueReady.notify_all();

    if (thread.joinable())
    {
        thread.join();
    }

    enabled = false;
}

void DbWorker::Post(Job job)
{
    {
        std::lock_guard<std::mutex> lock(queueLock);
        queue.push_back(std::move(job));
    }
    queueReady.notify_one();
}

void DbWorker::RunSync(Job job)
{
    std::mutex doneLock;
    std::condition_variable doneSignal;
    bool done = false;

    Post([&](Database& db)
    {
        job(db);
        {
            std::lock_guard<std::mutex> lock(doneLock);
            done = true;
        }
        doneSignal.notify_one();
    });

    std::unique_lock<std::mutex> lock(doneLock);
    doneSignal.wait(lock, [&]() { return done; });
}

std::size_t DbWorker::GetQueueSize()
{
    std::lock_guard<std::mutex> lock(queueLock);
    return queue.size();
}

void DbWorker::Run()
{
    while (true)
    {
        Job job;

        {
            std::unique_lock<std::mutex> lock(queueLock);
            queueReady.wait(lock, [this]() { return stopping || queue.empty() == false; });

            if (queue.empty())
            {
                return;   // stopping이고 남은 일도 없다
            }

            job = std::move(queue.front());
            queue.pop_front();
        }

        job(db);   // 락 밖에서 실행. 쿼리 도는 동안에도 다른 스레드가 작업을 넣을 수 있다
    }
}

std::string LoadDbConnectionString()
{
    const char* candidates[] = { "db_config.txt", "../db_config.txt", "../../db_config.txt", "../../../Server/db_config.txt" };

    for (const char* path : candidates)
    {
        std::ifstream file(path);

        if (file.is_open() == false)
        {
            continue;
        }

        std::string line;

        while (std::getline(file, line))
        {
            if (line.empty() == false && line.back() == '\r')
            {
                line.pop_back();
            }

            // 메모장이 UTF-8로 저장하면 파일 맨 앞에 BOM(EF BB BF)이 붙는다. 그대로 두면 첫 줄 주석을 못 알아본다.
            if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
            {
                line.erase(0, 3);
            }

            if (line.empty() == false && line[0] != '#')
            {
                std::cout << "[DB] config: " << path << std::endl;
                return line;
            }
        }
    }

    return std::string();
}
