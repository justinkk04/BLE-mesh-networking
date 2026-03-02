// D3.js Mesh Topology Graph — Midnight Vercel
const svg = d3.select('#mesh-graph');
let width = 400;
let height = 250;

let simulation;
let linkGroup, nodeGroup, defsGroup;
let nodesData = [];
let linksData = [];
let currentNodesStr = "";

export function init() {
    const svgEl = document.getElementById('mesh-graph');
    if (svgEl) {
        const rect = svgEl.getBoundingClientRect();
        width = rect.width > 0 ? rect.width : 400;
        height = rect.height > 0 ? rect.height : 250;
    }

    svg.attr("viewBox", [0, 0, width, height]);

    // Resize observer (Simplified, no crazy physics overrides)
    const ro = new ResizeObserver(entries => {
        for (let entry of entries) {
            if (entry.contentRect.width > 0 && entry.contentRect.height > 0) {
                width = entry.contentRect.width;
                height = entry.contentRect.height;
                svg.attr("viewBox", [0, 0, width, height]);
                if (simulation) {
                    simulation.force("center", d3.forceCenter(width / 2, height / 2));
                    simulation.alpha(0.1).restart();
                }
            }
        }
    });

    requestAnimationFrame(() => {
        const topoSection = document.getElementById('topology-section');
        if (topoSection) ro.observe(topoSection);
    });

    // Define filters once
    defsGroup = svg.append("defs");

    // Glow filter for connected node
    const glow = defsGroup.append("filter").attr("id", "glow");
    glow.append("feGaussianBlur").attr("stdDeviation", "3").attr("result", "coloredBlur");
    const feMerge = glow.append("feMerge");
    feMerge.append("feMergeNode").attr("in", "coloredBlur");
    feMerge.append("feMergeNode").attr("in", "SourceGraphic");

    // Subtle outer glow for gateway
    const gatewayGlow = defsGroup.append("filter").attr("id", "gateway-glow");
    gatewayGlow.append("feGaussianBlur").attr("stdDeviation", "4").attr("result", "blur");
    const gwMerge = gatewayGlow.append("feMerge");
    gwMerge.append("feMergeNode").attr("in", "blur");
    gwMerge.append("feMergeNode").attr("in", "SourceGraphic");

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
    const allNodeIds = Object.keys(meshNodes).sort();
    const newNodesStr = allNodeIds.join(',');

    // Data-only update (no topology change) -> just refresh colors
    if (newNodesStr === currentNodesStr && nodesData.length > 0) {
        nodeGroup.selectAll('.node-circle')
            .transition().duration(400)
            .attr("fill", d => getNodeColor(d, meshNodes))
            .attr("opacity", d => getNodeOpacity(d, meshNodes));
        nodeGroup.selectAll('.node-label')
            .attr("fill", d => getNodeOpacity(d, meshNodes) < 0.5 ? 'var(--text-dim)' : 'var(--text-secondary)');
        return;
    }

    currentNodesStr = newNodesStr;

    // Full topology rebuild
    nodesData = [{ id: "pi5", type: "gateway", label: "Pi 5" }];
    linksData = [];

    if (allNodeIds.length > 0) {
        // Use the dynamically reported connected node from the gateway state, fallback to 0 or first node
        let rootNodeId;
        if (gatewayAddress && allNodeIds.includes(gatewayAddress.toString())) {
            rootNodeId = gatewayAddress.toString();
        } else {
            rootNodeId = allNodeIds.includes("0") ? "0" : allNodeIds[0];
        }

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
    if (d.type === 'gateway') return "hsl(270, 75%, 70%)";  // Purple

    const nodeData = meshNodes[d.id];
    if (!nodeData || !nodeData.last_seen) {
        if (d.type === 'connected') return "hsl(35, 85%, 55%)";
        return "hsl(145, 65%, 50%)";
    }

    const age = Date.now() / 1000 - nodeData.last_seen;
    if (age > 30) return "hsl(5, 90%, 60%)";      // Red = offline
    if (age > 10) return "hsl(35, 85%, 55%)";     // Orange = stale
    if (d.type === 'connected') return "hsl(35, 85%, 55%)";
    return "hsl(145, 65%, 50%)";                   // Green = online
}

function getNodeOpacity(d, meshNodes) {
    if (d.type === 'gateway') return 1;
    const nodeData = meshNodes[d.id];
    if (!nodeData || !nodeData.last_seen) return 0.8;
    const age = Date.now() / 1000 - nodeData.last_seen;
    if (age > 30) return 0.35;
    return 1;
}

function render(meshNodes) {
    linkGroup.selectAll("*").remove();
    nodeGroup.selectAll("*").remove();

    // Draw solid links (no CSS animation to prevent ResizeObserver infinite loops)
    linkGroup.selectAll("line")
        .data(linksData)
        .enter().append("line")
        .attr("class", "mesh-link")
        .attr("stroke", "rgba(255,255,255,0.08)")
        .attr("stroke-width", 1.5);

    // Draw node groups
    const nodeG = nodeGroup.selectAll("g")
        .data(nodesData, d => d.id)
        .enter().append("g")
        .attr("class", "node-group")
        .style("cursor", "grab")
        .call(d3.drag()
            .on("start", dragstarted)
            .on("drag", dragged)
            .on("end", dragended));

    // Outer glow ring for gateway
    nodeG.filter(d => d.type === 'gateway').append("circle")
        .attr("r", 28)
        .attr("fill", "none")
        .attr("stroke", "hsla(270, 75%, 70%, 0.15)")
        .attr("stroke-width", 1);

    nodeG.append("circle")
        .attr("class", "node-circle")
        .attr("r", d => d.type === 'gateway' ? 20 : 16)
        .attr("fill", d => getNodeColor(d, meshNodes || {}))
        .attr("opacity", d => getNodeOpacity(d, meshNodes || {}))
        .attr("stroke", d => d.type === 'connected' ? "rgba(255,255,255,0.4)" : "none")
        .attr("stroke-width", 1.5)
        .attr("filter", d => d.type === 'gateway' ? "url(#gateway-glow)" : (d.type === 'connected' ? "url(#glow)" : null));

    nodeG.append("text")
        .attr("class", "node-label")
        .text(d => d.label)
        .attr("y", 30)
        .attr("text-anchor", "middle")
        .attr("fill", "var(--text-dim)")
        .attr("font-size", "10px")
        .attr("font-family", "var(--font)")
        .attr("font-weight", "500");

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

export function refresh() {
    if (simulation && width > 0 && height > 0) {
        simulation.force("center", d3.forceCenter(width / 2, height / 2));
        simulation.alpha(0.3).restart();
    }
}
