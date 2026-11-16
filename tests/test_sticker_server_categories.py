from __future__ import annotations

import copy
from pathlib import Path
import unittest
from unittest.mock import patch

from owner.stickers import compiler, server


class FakeBackend:
    def __init__(self):
        self.tables = {"tbl_StickerItem": {}, "tbl_ItemCategory": {}}
        self.ledgers = {
            "sdgo_StickerImportRows": {},
            "sdgo_StickerImportCategoryRows": None,
        }
        self.connections = []
        self.sql = []
        self.commit_count = 0
        self.corrupt_category_readback = False

    def connect(self, _):
        role = "write" if not self.connections else "verify"
        connection = FakeConnection(self, role)
        self.connections.append(connection)
        return connection

    def seed_owned_sticker(self, plan):
        row = copy.deepcopy(plan["rows"][0])
        self.tables["tbl_StickerItem"][row["StickerID"]] = row
        self.ledgers["sdgo_StickerImportRows"][row["StickerID"]] = {
            "OwnerKey": plan["owner"],
            "RowHash": compiler.sha(compiler.json_bytes(row)),
            "Revision": "legacy-revision",
        }


class FakeConnection:
    def __init__(self, backend, role):
        self.backend = backend
        self.role = role
        self.tables = copy.deepcopy(backend.tables)
        self.ledgers = copy.deepcopy(backend.ledgers)
        self.commits = 0
        self.rollbacks = 0
        self.closed = False
        self.category_written = False
        self.nextset_calls = 0
        self.transaction_primed = False

    def cursor(self):
        return FakeCursor(self)

    def commit(self):
        self.backend.tables = copy.deepcopy(self.tables)
        self.backend.ledgers = copy.deepcopy(self.ledgers)
        self.commits += 1
        self.backend.commit_count += 1

    def rollback(self):
        self.rollbacks += 1

    def close(self):
        self.closed = True


class FakeCursor:
    def __init__(self, connection):
        self.connection = connection
        self.description = None
        self._row = None
        self._lock_result_pending = False
        self._lock_code = None
        self._prime_fetch_pending = False

    @staticmethod
    def _normalized(sql):
        return " ".join(sql.split())

    def execute(self, sql, *params):
        normalized = self._normalized(sql)
        upper = normalized.upper()
        self.connection.backend.sql.append((normalized, params, self.connection.role))
        self.description = None
        self._row = None

        if upper.startswith("SET NOCOUNT ON;"):
            return self
        if upper == "SELECT TOP (0) STICKERID FROM DBO.TBL_STICKERITEM":
            self.description = (("StickerID",),)
            self._prime_fetch_pending = True
            return self
        if "SP_GETAPPLOCK" in upper:
            self._lock_result_pending = True
            self._lock_code = 0 if self.connection.transaction_primed else -999
            return self
        if upper.startswith("SELECT OBJECT_ID("):
            table = self._ledger_table(upper)
            self.description = (("OBJECT_ID",),)
            self._row = (1 if self.connection.ledgers[table] is not None else None,)
            return self
        if upper.startswith("SELECT OWNERKEY,ROWHASH,REVISION FROM DBO."):
            table, identity = self._ledger_query(upper, params)
            value = self.connection.ledgers[table].get(identity)
            self.description = (("OwnerKey",), ("RowHash",), ("Revision",))
            self._row = (
                (value["OwnerKey"], value["RowHash"], value["Revision"])
                if value
                else None
            )
            return self
        if upper.startswith("SELECT OWNERKEY,ROWHASH FROM DBO."):
            table, identity = self._ledger_query(upper, params)
            ledger = self.connection.ledgers[table]
            value = ledger.get(identity) if ledger is not None else None
            self.description = (("OwnerKey",), ("RowHash",))
            self._row = (value["OwnerKey"], value["RowHash"]) if value else None
            return self
        if upper.startswith("SELECT ") and " FROM DBO.TBL_STICKERITEM" in upper:
            self._select_native(
                "tbl_StickerItem", server.STICKER_COLUMNS, params[0]
            )
            return self
        if upper.startswith("SELECT ") and " FROM DBO.TBL_ITEMCATEGORY" in upper:
            self._select_native(
                "tbl_ItemCategory", server.CATEGORY_COLUMNS, params[0]
            )
            return self
        if upper.startswith("IF OBJECT_ID(") and "CREATE TABLE" in upper:
            table = self._ledger_table(upper)
            if self.connection.ledgers[table] is None:
                self.connection.ledgers[table] = {}
            return self
        if upper.startswith("UPDATE DBO.TBL_STICKERITEM SET "):
            self._update_native("tbl_StickerItem", server.STICKER_COLUMNS, params)
            return self
        if upper.startswith("UPDATE DBO.TBL_ITEMCATEGORY SET "):
            self._update_native("tbl_ItemCategory", server.CATEGORY_COLUMNS, params)
            self.connection.category_written = True
            return self
        if upper.startswith("INSERT DBO.TBL_STICKERITEM("):
            self._insert_native("tbl_StickerItem", server.STICKER_COLUMNS, params)
            return self
        if upper.startswith("INSERT DBO.TBL_ITEMCATEGORY("):
            self._insert_native("tbl_ItemCategory", server.CATEGORY_COLUMNS, params)
            self.connection.category_written = True
            return self
        if upper.startswith("DELETE DBO.SDGO_STICKERIMPORTROWS "):
            self.connection.ledgers["sdgo_StickerImportRows"].pop(params[0], None)
            return self
        if upper.startswith("DELETE DBO.SDGO_STICKERIMPORTCATEGORYROWS "):
            self.connection.ledgers["sdgo_StickerImportCategoryRows"].pop(
                params[0], None
            )
            return self
        if upper.startswith("INSERT DBO.SDGO_STICKERIMPORTROWS("):
            self._insert_ledger("sdgo_StickerImportRows", params)
            return self
        if upper.startswith("INSERT DBO.SDGO_STICKERIMPORTCATEGORYROWS("):
            self._insert_ledger("sdgo_StickerImportCategoryRows", params)
            return self
        raise AssertionError("unexpected SQL: " + normalized)

    def _ledger_table(self, upper):
        if "SDGO_STICKERIMPORTCATEGORYROWS" in upper:
            return "sdgo_StickerImportCategoryRows"
        if "SDGO_STICKERIMPORTROWS" in upper:
            return "sdgo_StickerImportRows"
        raise AssertionError("unknown ledger SQL: " + upper)

    def _ledger_query(self, upper, params):
        return self._ledger_table(upper), params[0]

    def _select_native(self, table, columns, identity):
        row = self.connection.tables[table].get(identity)
        if (
            row
            and table == "tbl_ItemCategory"
            and self.connection.role == "write"
            and self.connection.category_written
            and self.connection.backend.corrupt_category_readback
        ):
            row = {**row, "Option2": row["Option2"] + 1}
        self.description = tuple((column,) for column in columns)
        self._row = tuple(row[column] for column in columns) if row else None

    def _update_native(self, table, columns, params):
        identity = params[-1]
        self.connection.tables[table][identity] = dict(
            zip(columns, (identity, *params[:-1]))
        )

    def _insert_native(self, table, columns, params):
        row = dict(zip(columns, params))
        self.connection.tables[table][row[columns[0]]] = row

    def _insert_ledger(self, table, params):
        identity, owner, row_hash, revision = params
        self.connection.ledgers[table][identity] = {
            "OwnerKey": owner,
            "RowHash": row_hash,
            "Revision": revision,
        }

    def fetchone(self):
        return self._row

    def fetchall(self):
        if self._prime_fetch_pending:
            self._prime_fetch_pending = False
            self.connection.transaction_primed = True
        return []

    def nextset(self):
        self.connection.nextset_calls += 1
        if not self._lock_result_pending:
            return False
        self._lock_result_pending = False
        self.description = (("lock_result",),)
        self._row = (self._lock_code,)
        return True


class StickerServerCategoryTests(unittest.TestCase):
    def setUp(self):
        self.row = {
            "StickerID": 8_000_101,
            "ItemName": "sdgo-sticker-8000101",
            "WebFilename1": "",
            "WebFilename2": "",
            "UserLevel": 0,
            "AllowGroupInfluence": 0,
            "AllowInfluence": 0,
            "AllowUnitID": 0,
            "StickerType": 1,
        }
        self.category = {
            "CatalogueID": 1_790_101,
            "Name": "sdgo-sticker-category-1790101",
            "RefIndex": self.row["StickerID"],
            "TimeLimit": 0,
            "Option1": 0,
            "Option2": 0,
        }
        payload = {
            "schema": "sdgo.sticker-server-plan/1",
            "owner": "a" * 32,
            "rows": [self.row],
            "categories": [self.category],
            "client_record_sha256": "1" * 64,
            "client_category_sha256": "2" * 64,
            "credentials_included": False,
            "player_grants": False,
        }
        payload["revision"] = compiler.sha(compiler.json_bytes(payload))
        self.plan = payload
        self.host = {"role": "world", "path": "fixture"}

    def _patches(self, backend):
        return (
            patch.object(server, "connect", side_effect=backend.connect),
            patch.object(server, "world_ini", return_value=Path("WorldServer.ini")),
        )

    def test_preflight_reports_owned_sticker_and_missing_category(self):
        backend = FakeBackend()
        backend.seed_owned_sticker(self.plan)
        connect_patch, ini_patch = self._patches(backend)
        with connect_patch, ini_patch:
            result = server.preflight(self.host, self.plan)

        self.assertTrue(result["existing"])
        self.assertTrue(result["owned"])
        self.assertTrue(result["matches"])
        self.assertFalse(result["category_existing"])
        self.assertFalse(result["category_owned"])
        self.assertFalse(result["category_matches"])
        self.assertEqual(self.row["StickerID"], result["native_id"])
        self.assertEqual(self.category["CatalogueID"], result["category_id"])
        self.assertEqual(0, backend.commit_count)
        self.assertEqual(1, backend.connections[0].rollbacks)

    def test_preflight_refuses_baseline_category_without_owned_ledger(self):
        backend = FakeBackend()
        backend.seed_owned_sticker(self.plan)
        backend.tables["tbl_ItemCategory"][self.category["CatalogueID"]] = {
            **self.category,
            "Name": "baseline-category",
        }
        connect_patch, ini_patch = self._patches(backend)
        with connect_patch, ini_patch:
            with self.assertRaisesRegex(ValueError, "物品类别编号已被原版"):
                server.preflight(self.host, self.plan)

        self.assertEqual("baseline-category", backend.tables["tbl_ItemCategory"][
            self.category["CatalogueID"]
        ]["Name"])
        self.assertIsNone(backend.ledgers["sdgo_StickerImportCategoryRows"])
        self.assertEqual(0, backend.commit_count)
        self.assertEqual(1, backend.connections[0].rollbacks)

    def test_preflight_refuses_modified_owned_category(self):
        backend = FakeBackend()
        backend.seed_owned_sticker(self.plan)
        original = copy.deepcopy(self.category)
        backend.tables["tbl_ItemCategory"][original["CatalogueID"]] = {
            **original,
            "Name": "edited-after-registration",
        }
        backend.ledgers["sdgo_StickerImportCategoryRows"] = {
            original["CatalogueID"]: {
                "OwnerKey": self.plan["owner"],
                "RowHash": compiler.sha(compiler.json_bytes(original)),
                "Revision": "legacy-revision",
            }
        }
        connect_patch, ini_patch = self._patches(backend)
        with connect_patch, ini_patch:
            with self.assertRaisesRegex(ValueError, "物品类别存在未登记修改"):
                server.preflight(self.host, self.plan)

        self.assertEqual("edited-after-registration", backend.tables[
            "tbl_ItemCategory"
        ][original["CatalogueID"]]["Name"])
        self.assertEqual(0, backend.commit_count)
        self.assertEqual(1, backend.connections[0].rollbacks)

    def test_apply_legacy_sticker_adds_category_in_one_transaction(self):
        backend = FakeBackend()
        backend.seed_owned_sticker(self.plan)
        connect_patch, ini_patch = self._patches(backend)
        with connect_patch, ini_patch, patch.object(
            server.compiler, "server_plan", return_value=self.plan
        ):
            result = server.apply(self.host, {"fixture": True})

        self.assertTrue(result["applied"])
        self.assertFalse(result["player_grants"])
        self.assertEqual(self.row["StickerID"], result["native_id"])
        self.assertEqual(self.category["CatalogueID"], result["category_id"])
        self.assertEqual(1, backend.commit_count)
        self.assertEqual(2, len(backend.connections))
        self.assertEqual(1, backend.connections[0].commits)
        self.assertEqual(0, backend.connections[0].rollbacks)
        self.assertEqual(1, backend.connections[0].nextset_calls)
        self.assertEqual(1, backend.connections[1].rollbacks)
        self.assertEqual(
            self.category,
            backend.tables["tbl_ItemCategory"][self.category["CatalogueID"]],
        )
        self.assertEqual(
            self.plan["owner"],
            backend.ledgers["sdgo_StickerImportCategoryRows"][
                self.category["CatalogueID"]
            ]["OwnerKey"],
        )
        native_writes = [
            sql.upper()
            for sql, _, role in backend.sql
            if role == "write"
            and (sql.upper().startswith("INSERT DBO.TBL_")
                 or sql.upper().startswith("UPDATE DBO.TBL_"))
        ]
        self.assertFalse(any("TBL_STICKERITEM" in sql for sql in native_writes))
        self.assertEqual(1, sum("TBL_ITEMCATEGORY" in sql for sql in native_writes))
        all_sql = "\n".join(sql.upper() for sql, _, _ in backend.sql)
        self.assertIn("SET NOCOUNT ON", all_sql)
        self.assertNotIn("BEGIN TRANSACTION", all_sql)
        self.assertNotIn("TBLS_STOREITEMS", all_sql)
        self.assertNotIn("TBLS_SUBITEMS", all_sql)
        self.assertNotIn("TBL_USERITEM", all_sql)

    def test_zero_row_prime_precedes_app_lock_and_single_commit(self):
        backend = FakeBackend()
        backend.seed_owned_sticker(self.plan)
        connect_patch, ini_patch = self._patches(backend)
        with connect_patch, ini_patch, patch.object(
            server.compiler, "server_plan", return_value=self.plan
        ):
            server.apply(self.host, {"fixture": True})

        write_sql = [
            sql.upper() for sql, _, role in backend.sql if role == "write"
        ]
        set_index = next(
            index for index, sql in enumerate(write_sql)
            if sql.startswith("SET NOCOUNT ON;")
        )
        prime_index = write_sql.index(
            "SELECT TOP (0) STICKERID FROM DBO.TBL_STICKERITEM"
        )
        lock_index = next(
            index for index, sql in enumerate(write_sql) if "SP_GETAPPLOCK" in sql
        )
        self.assertLess(set_index, prime_index)
        self.assertLess(prime_index, lock_index)
        self.assertTrue(backend.connections[0].transaction_primed)
        self.assertEqual(1, backend.connections[0].nextset_calls)
        self.assertEqual(1, backend.connections[0].commits)
        self.assertEqual(1, backend.commit_count)
        self.assertFalse(any("BEGIN TRANSACTION" in sql for sql in write_sql))

    def test_category_readback_failure_rolls_back_sticker_and_category(self):
        backend = FakeBackend()
        backend.seed_owned_sticker(self.plan)
        before_tables = copy.deepcopy(backend.tables)
        before_ledgers = copy.deepcopy(backend.ledgers)
        backend.corrupt_category_readback = True
        connect_patch, ini_patch = self._patches(backend)
        with connect_patch, ini_patch, patch.object(
            server.compiler, "server_plan", return_value=self.plan
        ):
            with self.assertRaisesRegex(ValueError, "物品类别读回失败"):
                server.apply(self.host, {"fixture": True})

        self.assertEqual(before_tables, backend.tables)
        self.assertEqual(before_ledgers, backend.ledgers)
        self.assertEqual(0, backend.commit_count)
        self.assertEqual(1, len(backend.connections))
        self.assertEqual(0, backend.connections[0].commits)
        self.assertEqual(1, backend.connections[0].rollbacks)


if __name__ == "__main__":
    unittest.main()
