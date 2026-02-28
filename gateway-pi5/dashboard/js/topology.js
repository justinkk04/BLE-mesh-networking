// D3.js Mesh Topology Graph
const svg = d3.select('#mesh-graph');
let width = 400;
let height = 250;

let simulation;
let linkGroup, nodeGroup, defsGroup;
let nodesData = [];
let linksData = [];
let currentNodesStr = "";

export function init() {
    const rect = svg.node().getBoundingClientRect();
    width = rect.width || 400;
    height = rect.height || 250;
    svg.attr("viewBox", [0, 0, width, height]);

    // Resize observer
    const ro = new ResizeObserver(entries => {
        for (let entry of entries) {
            width = entry.contentRect.width || 400;
            height = entry.contentRect.height || 250;
            svg.attr("viewBox", [0, 0, width, height]);
            if (simulation) {
                simulation.force("center", d3.forceCenter(width / 2, height / 2));
                simulation.alpha(0.15).restart();
            }
        }
    });
    ro.observe(document.getElementById('topology-section'));

    // Define glow filter once
    defsGroup = svg.append("defs");
    const filter = defsGroup.append("filter").attr("id", "glow");
    filter.append("feGaussianBlur").attr("stdDeviation", "3").attr("result", "coloredBlur");
    const feMerge = filter.append("feMerge");
    feMerge.append("feMergeNode").attr("in", "coloredBlur");
    feMerge.append("feMergeNode").attr("in", "SourceGraphic");

    // Container groups
    linkGroup = svg.append("g").attr("class", "links");
    nodeGroup = svg.append("g").attr("class", "nodes");

    simulation = d3.forceSimulation()
        .force("charge", d3.forceManyBody().strength(-200))
        .force("center", d3.forceCenter(width / 2, height / 2))
        .force("link", d3.forceLink().id(d => d.id).distance(80))
        .force("collide", d3.forceCollide().radius(35))
        .on("tick", ticked);
}

export function updateGraph(meshNodes, gatewayAddress) {
    // Show ALL known nodes (including offline ones — they just get dimmer)
    const allNodeIds = Object.keys(meshNodes).sort();
    const newNodesStr = allNodeIds.join(',');

    // Data-only update (no topology change) -> just refresh colors
    if (newNodesStr === currentNodesStr && nodesData.length > 0) {
        nodeGroup.selectAll('.node-circle')
            .attr("fill", d => getNodeColor(d, meshNodes))
            .attr("opacity", d => getNodeOpacity(d, meshNodes));
        nodeGroup.selectAll('.node-label')
            .attr("fill", d => getNodeOpacity(d, meshNodes) < 0.5 ? 'var(--text-dim)' : 'var(--text-secondary)');
        return;
    }

    currentNodesStr = newNodesStr;

    // Full topology rebuild
    nodesData = [{ id: "pi5", type: "gateway", label: "Pi 5 Gateway" }];
    linksData = [];

    if (allNodeIds.length > 0) {
        // The first node (usually "0") is the directly connected GATT node
        let rootNodeId = allNodeIds.includes("0") ? "0" : allNodeIds[0];

        nodesData.push({ id: rootNodeId, type: "connected", label: `Node ${rootNodeId} (GATT)` });
        linksData.push({ source: "pi5", target: rootNodeId });

        for (const nid of allNodeIds) {
            if (nid !== rootNodeId) {
                nodesData.push({ id: nid, type: "remote", label: `Node ${nid}` });
                linksData.push({ source: rootNodeId, target: nid });
            }
        }
    }

    render(meshNodes);
}

function getNodeColor(d, meshNodes) {
    if (d.type === 'gateway') return "#bc8cff";

    const nodeData = meshNodes[d.id];
    if (!nodeData || !nodeData.last_seen) {
        if (d.type === 'connected') return "#d29922";
        return "#3fb950";
    }

    const age = Date.now() / 1000 - nodeData.last_seen;
    if (age > 30) return "#f85149";     // Red = offline
    if (age > 10) return "#d29922";     // Orange = stale
    if (d.type === 'connected') return "#d29922";
    return "#3fb950";                   // Green = online
}

function getNodeOpacity(d, meshNodes) {
    if (d.type === 'gateway') return 1;
    const nodeData = meshNodes[d.id];
    if (!nodeData || !nodeData.last_seen) return 0.8;
    const age = Date.now() / 1000 - nodeData.last_seen;
    if (age > 30) return 0.4;
    return 1;
}

function render(meshNodes) {
    // Clear old elements
    linkGroup.selectAll("*").remove();
    nodeGroup.selectAll("*").remove();

    // Draw links
    linkGroup.selectAll("line")
        .data(linksData)
        .enter().append("line")
        .attr("stroke", "rgba(255,255,255,0.15)")
        .attr("stroke-width", 2);

    // Draw node groups
    const nodeG = nodeGroup.selectAll("g")
        .data(nodesData, d => d.id)
        .enter().append("g")
        .attr("class", "node-group")
        .call(d3.drag()
            .on("start", dragstarted)
            .on("drag", dragged)
            .on("end", dragended));

    nodeG.append("circle")
        .attr("class", "node-circle")
        .attr("r", d => d.type === 'gateway' ? 22 : 18)
        .attr("fill", d => getNodeColor(d, meshNodes || {}))
        .attr("opacity", d => getNodeOpacity(d, meshNodes || {}))
        .attr("stroke", d => d.type === 'connected' ? "rgba(255,255,255,0.6)" : "none")
        .attr("stroke-width", 2)
        .attr("filter", d => d.type === 'connected' ? "url(#glow)" : null);

    nodeG.append("text")
        .attr("class", "node-label")
        .text(d => d.label)
        .attr("y", 32)
        .attr("text-anchor", "middle")
        .attr("fill", "var(--text-secondary)")
        .attr("font-size", "11px")
        .attr("font-family", "var(--font)");

    // Restart simulation
    simulation.nodes(nodesData);
    simulation.force("link").links(linksData);
    simulation.alpha(0.4).restart();
}

function ticked() {
    linkGroup.selectAll("line")
        .attr("x1", d => d.source.x)
        .attr("y1", d => d.source.y)
        .attr("x2", d => d.target.x)
        .attr("y2", d => d.target.y);

    nodeGroup.selectAll("g")
        .attr("transform", d => `translate(${d.x},${d.y})`);
}

function dragstarted(event, d) {
    if (!event.active) simulation.alphaTarget(0.3).restart();
    d.fx = d.x;
    d.fy = d.y;
}

function dragged(event, d) {
    d.fx = event.x;
    d.fy = event.y;
}

function dragended(event, d) {
    if (!event.active) simulation.alphaTarget(0);
    d.fx = null;
    d.fy = null;
}
