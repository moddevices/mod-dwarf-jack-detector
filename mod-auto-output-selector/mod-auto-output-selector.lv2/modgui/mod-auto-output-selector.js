function (event) {
    function status_changed(status) {
        var status = parseInt(status);
        event.icon.find('.mod-auto-output-selector-status').css({
            'background-position': '0px -' + (status*76) + 'px'
        });
    }

    if (event.type === 'start') {
        for (var i in event.ports) {
            if (event.ports[i].symbol === 'Status') {
                status_changed(event.ports[i].value);
                break;
            }
        }
    }
    else if (event.symbol === 'Status') {
        status_changed(event.value);
    }
}
