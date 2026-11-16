"""Transactional, definition-only server registration; never grants inventory."""

from __future__ import annotations

import contextlib
from pathlib import Path

from manager.database import connect
from . import compiler

STICKER_COLUMNS = (
    "StickerID",
    "ItemName",
    "WebFilename1",
    "WebFilename2",
    "UserLevel",
    "AllowGroupInfluence",
    "AllowInfluence",
    "AllowUnitID",
    "StickerType",
)
CATEGORY_COLUMNS = (
    "CatalogueID",
    "Name",
    "RefIndex",
    "TimeLimit",
    "Option1",
    "Option2",
)
COLUMNS = STICKER_COLUMNS


def world_ini(host):
    if not isinstance(host, dict) or host.get("role") != "world":
        raise ValueError("请选择世界服务器")
    from .deployment import safe

    root = Path(host["path"])
    for name in ("Server.ini", "WorldServer.ini"):
        path = safe(root, name)
        if path.is_file():
            return path
    raise ValueError("所选世界服务器缺少原生 INI 配置")


def _plan_rows(plan):
    if (
        not isinstance(plan, dict)
        or not isinstance(plan.get("rows"), list)
        or len(plan["rows"]) != 1
        or not isinstance(plan.get("categories"), list)
        or len(plan["categories"]) != 1
    ):
        raise ValueError("服务端纹章注册计划不完整")
    row = plan["rows"][0]
    category = plan["categories"][0]
    if set(row) != set(STICKER_COLUMNS) or set(category) != set(CATEGORY_COLUMNS):
        raise ValueError("服务端纹章注册计划字段无效")
    if (
        not isinstance(category["CatalogueID"], int)
        or isinstance(category["CatalogueID"], bool)
        or not 1_790_000 <= category["CatalogueID"] <= 1_799_999
        or category["RefIndex"] != row["StickerID"]
        or (category["TimeLimit"], category["Option1"], category["Option2"])
        != (0, 0, 0)
    ):
        raise ValueError("服务端纹章物品类别绑定无效")
    try:
        category["Name"].encode("ascii")
    except (AttributeError, UnicodeEncodeError):
        raise ValueError("服务端纹章物品类别名称必须为 ASCII 内部标识") from None
    return row, category


def _read_row(cursor, table, columns, key, identity, *, locked=False):
    selected = ",".join("[" + name + "]" for name in columns)
    lock = " WITH(UPDLOCK,HOLDLOCK)" if locked else ""
    return cursor.execute(
        "SELECT " + selected + " FROM dbo." + table + lock + " WHERE " + key + "=?",
        identity,
    ).fetchone()


def _ledger(cursor, table, key, identity):
    exists = (
        cursor.execute("SELECT OBJECT_ID('dbo." + table + "','U')").fetchone()[0]
        is not None
    )
    if not exists:
        return None
    return cursor.execute(
        "SELECT OwnerKey,RowHash FROM dbo." + table + " WHERE " + key + "=?",
        identity,
    ).fetchone()


def _check_row(
    cursor,
    plan,
    row,
    *,
    table,
    columns,
    key,
    ledger_table,
    collision_message,
    changed_message,
    inconsistent_message,
    locked,
):
    identity = row[key]
    current = _read_row(cursor, table, columns, key, identity, locked=locked)
    ledger = _ledger(cursor, ledger_table, key, identity)
    current_row = dict(zip(columns, current)) if current else None
    if current:
        if not ledger or ledger[0] != plan["owner"]:
            raise ValueError(collision_message)
        if compiler.sha(compiler.json_bytes(current_row)) != ledger[1]:
            raise ValueError(changed_message)
    elif ledger:
        raise ValueError(inconsistent_message)
    return {
        "existing": bool(current),
        "owned": bool(ledger),
        "matches": current_row == row,
    }


def check(cursor, plan, *, locked=False):
    row, category = _plan_rows(plan)
    sticker = _check_row(
        cursor,
        plan,
        row,
        table="tbl_StickerItem",
        columns=STICKER_COLUMNS,
        key="StickerID",
        ledger_table="sdgo_StickerImportRows",
        collision_message="服务端纹章编号已被原版或其他项目占用",
        changed_message="服务端纹章定义存在未登记修改，拒绝覆盖",
        inconsistent_message="服务端纹章归属记录与定义不一致",
        locked=locked,
    )
    category_state = _check_row(
        cursor,
        plan,
        category,
        table="tbl_ItemCategory",
        columns=CATEGORY_COLUMNS,
        key="CatalogueID",
        ledger_table="sdgo_StickerImportCategoryRows",
        collision_message="服务端纹章物品类别编号已被原版或其他项目占用",
        changed_message="服务端纹章物品类别存在未登记修改，拒绝覆盖",
        inconsistent_message="服务端纹章物品类别归属记录与定义不一致",
        locked=locked,
    )
    return {
        "existing": sticker["existing"],
        "owned": sticker["owned"],
        "category_existing": category_state["existing"],
        "category_owned": category_state["owned"],
        "matches": sticker["matches"],
        "category_matches": category_state["matches"],
        "native_id": row["StickerID"],
        "category_id": category["CatalogueID"],
    }


def preflight(host, plan):
    with contextlib.closing(connect(world_ini(host))) as db:
        try:
            return check(db.cursor(), plan)
        finally:
            db.rollback()


def _publish_row(cursor, row, *, table, columns, key, existing):
    values = [row[column] for column in columns]
    if existing:
        cursor.execute(
            "UPDATE dbo."
            + table
            + " SET "
            + ",".join("[" + column + "]=?" for column in columns[1:])
            + " WHERE "
            + key
            + "=?",
            *values[1:],
            values[0],
        )
    else:
        cursor.execute(
            "INSERT dbo."
            + table
            + "("
            + ",".join("[" + column + "]" for column in columns)
            + ") VALUES("
            + ",".join("?" for _ in columns)
            + ")",
            *values,
        )


def _replace_ledger(cursor, table, key, identity, owner, row_hash, revision):
    cursor.execute("DELETE dbo." + table + " WHERE " + key + "=?", identity)
    cursor.execute(
        "INSERT dbo."
        + table
        + "("
        + key
        + ",OwnerKey,RowHash,Revision) VALUES(?,?,?,?)",
        identity,
        owner,
        row_hash,
        revision,
    )


def _same_row(actual, columns, expected):
    return bool(actual) and dict(zip(columns, actual)) == expected


def apply(host, entry):
    plan = compiler.server_plan(entry)
    row, category = _plan_rows(plan)
    sticker_hash = compiler.sha(compiler.json_bytes(row))
    category_hash = compiler.sha(compiler.json_bytes(category))
    with contextlib.closing(connect(world_ini(host))) as db:
        try:
            # ODBC autocommit=False already owns one implicit transaction.
            # An explicit BEGIN creates a nested transaction on this host;
            # one db.commit() would then leave all changes uncommitted.
            cur = db.cursor()
            cur.execute(
                "SET NOCOUNT ON; SET XACT_ABORT ON; "
                "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE;"
            )
            # A warm native SQL session can execute SET/procedures without
            # starting its implicit transaction. A zero-row table SELECT starts
            # exactly one transaction before the transaction-owned app lock.
            cur.execute("SELECT TOP (0) StickerID FROM dbo.tbl_StickerItem").fetchall()
            cur.execute(
                "DECLARE @r int; EXEC @r=sys.sp_getapplock "
                "@Resource='SDGO:sticker-import',@LockMode='Exclusive',"
                "@LockOwner='Transaction',@LockTimeout=10000; SELECT @r;"
            )
            # Native SQL/ODBC versions may expose a procedure status before the
            # scalar result. Consume it instead of treating an empty set as a row.
            while cur.description is None:
                if not cur.nextset():
                    raise ValueError("纹章目录事务未返回锁结果")
            result = cur.fetchone()
            if not result or result[0] < 0:
                code=result[0] if result else 'missing'
                raise ValueError("纹章定义事务未取得锁（代码 "+str(code)+"），请稍后重试")
            before = check(cur, plan, locked=True)
            cur.execute(
                "IF OBJECT_ID('dbo.sdgo_StickerImportRows','U') IS NULL "
                "CREATE TABLE dbo.sdgo_StickerImportRows("
                "StickerID int NOT NULL PRIMARY KEY,OwnerKey varchar(32) NOT NULL,"
                "RowHash varchar(64) NOT NULL,Revision varchar(64) NOT NULL);"
            )
            cur.execute(
                "IF OBJECT_ID('dbo.sdgo_StickerImportCategoryRows','U') IS NULL "
                "CREATE TABLE dbo.sdgo_StickerImportCategoryRows("
                "CatalogueID int NOT NULL PRIMARY KEY,OwnerKey varchar(32) NOT NULL,"
                "RowHash varchar(64) NOT NULL,Revision varchar(64) NOT NULL);"
            )
            if not before["matches"]:
                _publish_row(
                    cur,
                    row,
                    table="tbl_StickerItem",
                    columns=STICKER_COLUMNS,
                    key="StickerID",
                    existing=before["existing"],
                )
            if not before["category_matches"]:
                _publish_row(
                    cur,
                    category,
                    table="tbl_ItemCategory",
                    columns=CATEGORY_COLUMNS,
                    key="CatalogueID",
                    existing=before["category_existing"],
                )
            _replace_ledger(
                cur,
                "sdgo_StickerImportRows",
                "StickerID",
                row["StickerID"],
                plan["owner"],
                sticker_hash,
                plan["revision"],
            )
            _replace_ledger(
                cur,
                "sdgo_StickerImportCategoryRows",
                "CatalogueID",
                category["CatalogueID"],
                plan["owner"],
                category_hash,
                plan["revision"],
            )
            actual_sticker = _read_row(
                cur,
                "tbl_StickerItem",
                STICKER_COLUMNS,
                "StickerID",
                row["StickerID"],
            )
            actual_category = _read_row(
                cur,
                "tbl_ItemCategory",
                CATEGORY_COLUMNS,
                "CatalogueID",
                category["CatalogueID"],
            )
            if not _same_row(actual_sticker, STICKER_COLUMNS, row):
                raise ValueError("服务端纹章定义读回失败")
            if not _same_row(actual_category, CATEGORY_COLUMNS, category):
                raise ValueError("服务端纹章物品类别读回失败")
            db.commit()
            # Read through a new connection: reads in the writing session are
            # insufficient evidence that the native SQL transaction persisted.
            with contextlib.closing(connect(world_ini(host))) as verify:
                try:
                    v = verify.cursor()
                    persisted_sticker = _read_row(
                        v,
                        "tbl_StickerItem",
                        STICKER_COLUMNS,
                        "StickerID",
                        row["StickerID"],
                    )
                    persisted_category = _read_row(
                        v,
                        "tbl_ItemCategory",
                        CATEGORY_COLUMNS,
                        "CatalogueID",
                        category["CatalogueID"],
                    )
                    sticker_ledger = v.execute(
                        "SELECT OwnerKey,RowHash,Revision "
                        "FROM dbo.sdgo_StickerImportRows WHERE StickerID=?",
                        row["StickerID"],
                    ).fetchone()
                    category_ledger = v.execute(
                        "SELECT OwnerKey,RowHash,Revision "
                        "FROM dbo.sdgo_StickerImportCategoryRows WHERE CatalogueID=?",
                        category["CatalogueID"],
                    ).fetchone()
                    if (
                        not _same_row(persisted_sticker, STICKER_COLUMNS, row)
                        or not _same_row(persisted_category, CATEGORY_COLUMNS, category)
                        or not sticker_ledger
                        or tuple(sticker_ledger)
                        != (plan["owner"], sticker_hash, plan["revision"])
                        or not category_ledger
                        or tuple(category_ledger)
                        != (plan["owner"], category_hash, plan["revision"])
                    ):
                        raise ValueError(
                            "纹章定义与物品类别提交后独立读回失败，请保留诊断并重新核对"
                        )
                finally:
                    verify.rollback()
            return {
                "applied": True,
                "revision": plan["revision"],
                "native_id": row["StickerID"],
                "category_id": category["CatalogueID"],
                "restart_required": True,
                "player_grants": False,
            }
        except Exception:
            db.rollback()
            raise
