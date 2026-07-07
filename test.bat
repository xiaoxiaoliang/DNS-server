@echo off
echo ==========================================
echo DNS Server Stress Test
echo 10 domains x 3 times + 3 nonexist x 3
echo ==========================================
echo.

echo --- [1/13] baidu.com x3 ---
for /L %%i in (1,1,3) do @nslookup -timeout=10 baidu.com 192.168.9.23 2>&1 | findstr "Address"
echo --- [2/13] qq.com x3 ---
for /L %%i in (1,1,3) do @nslookup -timeout=10 qq.com 192.168.9.23 2>&1 | findstr "Address"
echo --- [3/13] taobao.com x3 ---
for /L %%i in (1,1,3) do @nslookup -timeout=10 taobao.com 192.168.9.23 2>&1 | findstr "Address"
echo --- [4/13] jd.com x3 ---
for /L %%i in (1,1,3) do @nslookup -timeout=10 jd.com 192.168.9.23 2>&1 | findstr "Address"
echo --- [5/13] sina.com.cn x3 ---
for /L %%i in (1,1,3) do @nslookup -timeout=10 sina.com.cn 192.168.9.23 2>&1 | findstr "Address"
echo --- [6/13] 163.com x3 ---
for /L %%i in (1,1,3) do @nslookup -timeout=10 163.com 192.168.9.23 2>&1 | findstr "Address"
echo --- [7/13] sohu.com x3 ---
for /L %%i in (1,1,3) do @nslookup -timeout=10 sohu.com 192.168.9.23 2>&1 | findstr "Address"
echo --- [8/13] bilibili.com x3 ---
for /L %%i in (1,1,3) do @nslookup -timeout=10 bilibili.com 192.168.9.23 2>&1 | findstr "Address"
echo --- [9/13] zhihu.com x3 ---
for /L %%i in (1,1,3) do @nslookup -timeout=10 zhihu.com 192.168.9.23 2>&1 | findstr "Address"
echo --- [10/13] meituan.com x3 ---
for /L %%i in (1,1,3) do @nslookup -timeout=10 meituan.com 192.168.9.23 2>&1 | findstr "Address"

echo --- [11/13] nonexist123.com x3 ---
for /L %%i in (1,1,3) do @nslookup -timeout=10 nonexist123.com 192.168.9.23 2>&1 | findstr "timed\|Non-exist\|can't find"
echo --- [12/13] thisdoesnotexist.org x3 ---
for /L %%i in (1,1,3) do @nslookup -timeout=10 thisdoesnotexist.org 192.168.9.23 2>&1 | findstr "timed\|Non-exist\|can't find"
echo --- [13/13] fake789.net x3 ---
for /L %%i in (1,1,3) do @nslookup -timeout=10 fake789.net 192.168.9.23 2>&1 | findstr "timed\|Non-exist\|can't find"

echo.
echo ========== TEST COMPLETE ==========

