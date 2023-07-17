package command

import (
	"encoding/base64"
	"fmt"
	"io/ioutil"
	"os/user"
	"testing"

	"github.com/stretchr/testify/require"

	"github.com/alibaba/ilogtail/pkg/pipeline"
	"github.com/alibaba/ilogtail/plugins/test"
	"github.com/alibaba/ilogtail/plugins/test/mock"
)

func TestCommandTestCollecetUserBase64WithTimeout(t *testing.T) {
	ctx := mock.NewEmptyContext("project", "store", "config")
	p := pipeline.MetricInputs[pluginName]().(*InputCommand)
	c := new(test.MockMetricCollector)
	// Script set to sleep for 5 seconds
	scriptContent := `sleep 5 &&  echo -e "__labels__:a#\$#1|b#\$#2    __value__:0  __name__:metric_command_example \n __labels__:a#\$#3|b#\$#4    __value__:3  __name__:metric_command_example2"`

	// base64
	p.ScriptContent = base64.StdEncoding.EncodeToString([]byte(scriptContent))
	p.ContentEncoding = "Base64"
	p.TimeoutMilliSeconds = 6000
	p.ScriptType = "shell"
	p.User = "test"
	if _, err := p.Init(ctx); err != nil {
		t.Errorf("cannot init InputCommand: %v", err)
		return
	}
	err := p.Collect(c)
	if err == nil {
		t.Errorf("expect error with timeout")
	}

	// fmt.Println("--------labels-----", meta.Labels)
	shouldReturn := assertLogs(c, t, p)
	if shouldReturn {
		return
	}

}

func TestCommandTestCollecetUserBase64(t *testing.T) {
	ctx := mock.NewEmptyContext("project", "store", "config")
	p := pipeline.MetricInputs[pluginName]().(*InputCommand)
	c := new(test.MockMetricCollector)
	scriptContent := `echo -e "__labels__:a#\$#1|b#\$#2    __value__:0  __name__:metric_command_example \n __labels__:a#\$#3|b#\$#4    __value__:3  __name__:metric_command_example2"`
	// base64
	p.ScriptType = "shell"
	p.CmdPath = "/usr/bin/sh"
	p.ScriptContent = base64.StdEncoding.EncodeToString([]byte(scriptContent))
	p.ContentEncoding = "Base64"
	p.User = "root"
	if _, err := p.Init(ctx); err != nil {
		t.Errorf("cannot init InputCommand: %v", err)
		return
	}
	if err := p.Collect(c); err != nil {
		t.Errorf("Collect() error = %v", err)
		return
	}
	// fmt.Println("--------labels-----", meta.Labels)
	shouldReturn := assertLogs(c, t, p)
	if shouldReturn {
		return
	}
}

func TestCommandTestCollect(t *testing.T) {
	ctx := mock.NewEmptyContext("project", "store", "config")
	p := pipeline.MetricInputs[pluginName]().(*InputCommand)
	c := new(test.MockMetricCollector)

	p.ScriptContent = `cat /var/log/messages`
	p.ScriptType = "shell"
	p.ContentEncoding = "PlainText"
	p.User = "root"

	if _, err := p.Init(ctx); err != nil {
		t.Errorf("cannot init InputCommand: %v", err)
		return
	}
	if err := p.Collect(c); err != nil {
		t.Errorf("Collect() error = %v", err)
		return
	}

	shouldReturn := assertLogs(c, t, p)
	if shouldReturn {
		return
	}
}

func TestCommandTestExceptionCollect(t *testing.T) {
	ctx := mock.NewEmptyContext("project", "store", "config")
	p := pipeline.MetricInputs[pluginName]().(*InputCommand)
	c := new(test.MockMetricCollector)

	p.ScriptContent = `xxxxxX`
	p.ScriptType = "shell"
	p.ContentEncoding = "PlainText"
	p.User = "root"

	if _, err := p.Init(ctx); err != nil {
		t.Errorf("cannot init InputCommand: %v", err)
		return
	}
	if err := p.Collect(c); err != nil {
		t.Errorf("Collect() error = %v", err)
		return
	}

	shouldReturn := assertLogs(c, t, p)
	if shouldReturn {
		return
	}
}

func TestCommandTestTimeoutCollect(t *testing.T) {
	ctx := mock.NewEmptyContext("project", "store", "config")
	p := pipeline.MetricInputs[pluginName]().(*InputCommand)
	c := new(test.MockMetricCollector)

	p.ScriptContent = `sleep 10`
	p.ScriptType = "shell"
	p.ContentEncoding = "PlainText"
	p.User = "root"

	if _, err := p.Init(ctx); err != nil {
		t.Errorf("cannot init InputCommand: %v", err)
		return
	}
	if err := p.Collect(c); err != nil {
		t.Errorf("Collect() error = %v", err)
		return
	}

	shouldReturn := assertLogs(c, t, p)
	if shouldReturn {
		return
	}
}

func assertLogs(c *test.MockMetricCollector, t *testing.T, p *InputCommand) bool {
	for _, log := range c.Logs {
		fmt.Println("logs", log)
	}
	return false
}

func TestCommandTestInit(t *testing.T) {
	ctx := mock.NewEmptyContext("project", "store", "config")
	p := pipeline.MetricInputs[pluginName]().(*InputCommand)
	_, err := p.Init(ctx)
	require.Error(t, err)
	if err != nil {
		fmt.Println("default config error", err)
	}

	// Test for wrong script type
	p.ScriptType = "golang_none"
	_, err = p.Init(ctx)
	require.Error(t, err)
	if err != nil {
		fmt.Println("expect error with script type not support", err)
	}
	p.ScriptType = "bash"

	// Test the wrong User
	p.User = "root"
	_, err = p.Init(ctx)
	require.Error(t, err)
	if err != nil {
		fmt.Println("expect error with wrong user root", err)
	}
	p.User = "someone"

	// test contentType
	p.ContentEncoding = "mixin"
	_, err = p.Init(ctx)
	require.Error(t, err)
	if err != nil {
		fmt.Println("expect error with ContentType error", err)
	}
	p.ContentEncoding = defaultContentType

	// The test script content is empty
	p.ScriptContent = ""
	_, err = p.Init(ctx)
	require.Error(t, err)
	if err != nil {
		fmt.Println("expect error with ScriptContent empty error", err)
	}
	p.ScriptContent = "some"
	// Test execution script timed out
	// Test the dataType of the output
	p.IntervalMs = 3000
	p.TimeoutMilliSeconds = 4000
	_, err = p.Init(ctx)
	require.Error(t, err)
	if err != nil {
		fmt.Println("expect error with ExecScriptTimeOut > IntervalMs ", err)
	}
}

// test script storage
func TestScriptStorage(t *testing.T) {
	u, err := user.Current()
	fmt.Printf("Username %s\n", u.Username)

	content := `echo -e "__labels__:hostname#\$#idc_cluster_env_name|ip#\$#ip_address    __value__:0  __name__:metric_command_example"`
	storage := GetStorage("/data/workspaces/ilogtail/scriptStorage/")
	if storage.Err != nil {
		t.Errorf("create Storage error %s", storage.Err)
		return
	}
	filepath, err := storage.SaveContent(content, "TestScriptStorage", "shell")
	if err != nil {
		t.Errorf("ScriptStorage save content error %s", err)
		return
	}
	data, err := ioutil.ReadFile(filepath)
	if err != nil {
		t.Errorf("read file error")
		return
	}
	if string(data) != content {
		t.Errorf("content compare error")
		return
	}

	fmt.Print("\n---TestScriptStorage filepath", filepath, "\n")

	// Get again
	filepath, _ = storage.SaveContent(content, "TestScriptStorage", "shell")
	data, _ = ioutil.ReadFile(filepath)
	if string(data) != content {
		t.Errorf("content compare error")
		return
	}
}
