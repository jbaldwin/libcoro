@ECHO OFF
SET DIR=%~dp0
SET WRAPPER_JAR=%DIR%gradle\wrapper\gradle-wrapper.jar
IF NOT EXIST "%WRAPPER_JAR%" (
  ECHO gradle-wrapper.jar missing; please run "gradle wrapper" locally to generate full wrapper.
  gradle %*
  GOTO :EOF
)
java -jar "%WRAPPER_JAR%" %*
